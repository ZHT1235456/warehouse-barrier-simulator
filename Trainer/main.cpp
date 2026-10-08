#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <torch/torch.h>
using torch::Tensor;
using namespace torch::indexing;
struct NetImpl : torch::nn::Module
{
    torch::nn::Linear a{nullptr}, b{nullptr}, c{nullptr};
    NetImpl(int in, int width, int out)
    {
        a = register_module("a", torch::nn::Linear(in, width));
        b = register_module("b", torch::nn::Linear(width, width));
        c = register_module("c", torch::nn::Linear(width, out));
    }
    Tensor forward(Tensor x)
    {
        return c(torch::tanh(b(torch::tanh(a(x)))));
    }
};
TORCH_MODULE(Net);
struct System
{
    bool paper, dynamic;
    int ed;
    float dt = .05f, radius = .3f;
    int horizon = 60;
    explicit System(bool p, bool d = false) : paper(p), dynamic(d), ed(p ? 4 : (d ? 30 : 18))
    {
    }
    Tensor environments(int n)
    {
        auto e = torch::zeros({n, ed});
        if (paper)
        {
            e.index_put_({Slice(), 0}, 2.2 + torch::rand({n}) * 1.8);
            e.index_put_({Slice(), 1}, torch::rand({n}) * 1.2 - .6);
            e.index_put_({Slice(), Slice(2, 4)}, .5 + torch::rand({n, 2}) * .35);
        }
        else
        {
            for (int i = 0; i < 6; i++)
            {
                e.index_put_({Slice(), 3 * i}, float((i % 3) * 4 - 4) + (torch::rand({n}) - .5) * .6);
                e.index_put_({Slice(), 3 * i + 1}, float((i / 3) * 4 - 2) + (torch::rand({n}) - .5) * .6);
                e.index_put_({Slice(), 3 * i + 2}, .6 + torch::rand({n}) * .3);
            }
        }
        if (dynamic)
        {
            e.index_put_({Slice(), 12}, torch::rand({n}) * 6 - 3);
            e.index_put_({Slice(), 13}, 0);
            e.index_put_({Slice(), 14}, .45);
            e.index_put_({Slice(), 15}, 6);
            e.index_put_({Slice(), 16}, torch::rand({n}) * 6 - 3);
            e.index_put_({Slice(), 17}, .45);
            auto speed = .15 + torch::rand({n}) * .25;
            auto signs = torch::where(torch::rand({n}) > .5, torch::ones({n}), -torch::ones({n}));
            e.index_put_({Slice(), 26}, speed * signs);
            e.index_put_({Slice(), 29}, (.15 + torch::rand({n}) * .25) * signs);
        }
        return e;
    }
    Tensor states(int n)
    {
        auto x = torch::rand({n, 4});
        auto scale = paper ? torch::tensor({8.f, 8.f}) : torch::tensor({15.f, 11.f});
        auto offset = paper ? torch::tensor({4.f, 4.f}) : torch::tensor({7.5f, 5.5f});
        x.index_put_({Slice(), Slice(0, 2)}, x.index({Slice(), Slice(0, 2)}) * scale - offset);
        x.index_put_({Slice(), Slice(2, 4)}, x.index({Slice(), Slice(2, 4)}) * 4 - 2);
        return x;
    }
    Tensor features(Tensor x, Tensor e)
    {
        return torch::cat({x, e}, 1);
    }
    Tensor brake(Tensor x)
    {
        return torch::clamp(-2 * x.index({Slice(), Slice(2, 4)}), -1, 1);
    }
    Tensor step(Tensor x, Tensor u)
    {
        auto p = x.index({Slice(), Slice(0, 2)}), v = x.index({Slice(), Slice(2, 4)});
        return torch::cat({p + dt * v + .5f * dt * dt * u, v + dt * u}, 1);
    }
    Tensor evolve(Tensor e)
    {
        if (!dynamic)
            return e;
        auto next = e.clone();
        for (int i = 4; i < 6; i++)
            for (int j = 0; j < 2; j++)
            {
                auto pos = e.index({Slice(), 3 * i + j}) + dt * e.index({Slice(), 18 + 2 * i + j});
                float bound = j == 0 ? 7.f : 5.f;
                auto hit = pos.abs() > bound;
                auto reflected =
                    torch::where(pos > bound, 2 * bound - pos, torch::where(pos < -bound, -2 * bound - pos, pos));
                next.index_put_({Slice(), 3 * i + j}, reflected);
                next.index_put_({Slice(), 18 + 2 * i + j}, torch::where(hit, -e.index({Slice(), 18 + 2 * i + j}),
                                                                        e.index({Slice(), 18 + 2 * i + j})));
            }
        return next;
    }
    Tensor margin(Tensor x, Tensor e)
    {
        auto p = x.index({Slice(), Slice(0, 2)});
        std::vector<Tensor> cs;
        if (paper)
        {
            cs.push_back(torch::sqrt(p.square().sum(1) + 1e-9) - e.index({Slice(), 2}));
            cs.push_back(torch::sqrt((p - e.index({Slice(), Slice(0, 2)})).square().sum(1) + 1e-9) -
                         e.index({Slice(), 3}));
        }
        else
        {
            for (int i = 0; i < 6; i++)
                cs.push_back(torch::sqrt((p - e.index({Slice(), Slice(3 * i, 3 * i + 2)})).square().sum(1) + 1e-9) -
                             e.index({Slice(), 3 * i + 2}) - radius);
            cs.push_back(8 - torch::abs(p.index({Slice(), 0})) - radius);
            cs.push_back(6 - torch::abs(p.index({Slice(), 1})) - radius);
        }
        return std::get<0>(torch::stack(cs, 1).min(1));
    }
    Tensor switchU(Net s, Tensor x, Tensor e, bool grad)
    {
        auto z = s->forward(features(x, e));
        auto hard = torch::where(z >= 0, torch::ones_like(z), -torch::ones_like(z));
        auto soft = torch::tanh(z / .25);
        return grad ? hard.detach() - soft.detach() + soft : hard;
    }
    Tensor rollout(Tensor x, Tensor e, Net h, Net s, Net tau, int mode, bool grad, bool smooth)
    {
        std::vector<Tensor> cs{margin(x, e)};
        for (int t = 0; t < horizon; t++)
        {
            auto u = brake(x);
            if (mode)
            {
                auto us = switchU(s, x, e, grad);
                if (mode == 2)
                    u = us;
                else
                {
                    auto hv = h->forward(features(x, e)), tv = tau->forward(e);
                    auto g = grad ? torch::sigmoid((tv - hv) / .1) : (hv < tv).to(torch::kFloat32);
                    u = g * us + (1 - g) * u;
                }
            }
            x = step(x, u);
            e = evolve(e);
            cs.push_back(margin(x, e));
        }
        auto c = torch::stack(cs, 1);
        return smooth ? -.05 * torch::logsumexp(-c / .05, 1) : std::get<0>(c.min(1));
    }
};
void array(std::ostream &o, Tensor t)
{
    t = t.detach().to(torch::kCPU).contiguous().flatten();
    o << "[";
    auto p = t.data_ptr<float>();
    for (int64_t i = 0; i < t.numel(); i++)
    {
        if (i)
            o << ",";
        o << p[i];
    }
    o << "]";
}
void netjson(std::ostream &o, Net n)
{
    o << "[";
    int i = 0;
    for (auto l : {n->a, n->b, n->c})
    {
        if (i++)
            o << ",";
        o << "{\"in\":" << l->weight.size(1) << ",\"out\":" << l->weight.size(0) << ",\"weight\":";
        array(o, l->weight);
        o << ",\"bias\":";
        array(o, l->bias);
        o << "}";
    }
    o << "]";
}
void fixture(System &sys, Net h, Net s, Net tau, const std::string &out)
{
    torch::NoGradGuard no;
    auto e = sys.environments(3), x = sys.states(3);
    std::ofstream f(out + "/parity.json");
    f << std::setprecision(9) << "[";
    for (int i = 0; i < 3; i++)
    {
        if (i)
            f << ",";
        auto ee = e.index({Slice(i, i + 1)}), xx = x.index({Slice(i, i + 1)}), input = sys.features(xx, ee);
        f << "{\"input\":";
        array(f, input);
        f << ",\"env\":";
        array(f, ee);
        f << ",\"h\":";
        array(f, h->forward(input));
        f << ",\"s\":";
        array(f, s->forward(input));
        f << ",\"tau\":";
        array(f, tau->forward(ee));
        f << "}";
    }
    f << "]";
}
void evaluate(System &sys, Net h, Net s, Net tau, int seed, std::string stage, std::string out, int count)
{
    torch::NoGradGuard no;
    torch::manual_seed(90000 + seed);
    std::ofstream f(out + "/metrics_" + stage + ".csv");
    f << "seed,domain,method,success,hard_recovery,brake_safe_lost,effort,intervention,gate_usage\n";
    for (int domain = 0; domain < 13; domain++)
    {
        auto e = sys.environments(1).repeat({count, 1});
        if (domain >= 9)
        {
            if (sys.paper)
                e.index_put_({Slice(), 0}, e.index({Slice(), 0}) * (domain == 9 ? .7 : 1.3));
            else
                for (int k = 0; k < 6; k++)
                    e.index_put_({Slice(), 3 * k + 2}, e.index({Slice(), 3 * k + 2}) * (domain == 9 ? 1.3 : .7));
        }
        auto initial = sys.states(count);
        auto keep = (sys.margin(initial, e) >= 0);
        initial = initial.index({keep});
        e = e.index({keep});
        auto bs = sys.rollout(initial, e, h, s, tau, 0, false, false) >= 0;
        auto baseE = e.clone();
        for (int mode = 0; mode < 3; mode++)
        {
            e = baseE.clone();
            auto x = initial.clone();
            auto minc = sys.margin(x, e);
            float energy = 0, intervention = 0, usage = 0;
            for (int t = 0; t < sys.horizon; t++)
            {
                auto ub = sys.brake(x), u = ub;
                auto g = torch::zeros({x.size(0), 1});
                if (mode)
                {
                    auto us = sys.switchU(s, x, e, false);
                    g = mode == 2 ? torch::ones_like(g)
                                  : (h->forward(sys.features(x, e)) < tau->forward(e)).to(torch::kFloat32);
                    u = g * us + (1 - g) * ub;
                }
                energy += u.square().sum(1).mean().item<float>();
                intervention += (u - ub).norm(2, 1).mean().item<float>();
                usage += g.mean().item<float>();
                x = sys.step(x, u);
                e = sys.evolve(e);
                minc = torch::minimum(minc, sys.margin(x, e));
            }
            auto ok = minc >= 0;
            auto hard = ~bs;
            auto ratio = [](Tensor a, Tensor b) {
                return (a & b).sum().item<float>() / std::max(1.f, b.sum().item<float>());
            };
            f << seed << "," << domain << ","
              << (mode == 0   ? "brake"
                  : mode == 1 ? stage
                              : "full_boundary")
              << "," << ok.to(torch::kFloat32).mean().item<float>() << "," << ratio(ok, hard) << "," << ratio(~ok, bs)
              << "," << energy / sys.horizon << "," << intervention / sys.horizon << "," << usage / sys.horizon << "\n";
        }
    }
}
int main(int argc, char **argv)
{
    try
    {
        bool paper = false, smoke = false, checkExport = false, dynamic = false;
        int seed = 0, steps = 600, batch = 256;
        std::string out = "artifacts";
        for (int i = 1; i < argc; i++)
        {
            std::string a = argv[i];
            if (a == "--dynamic")
                dynamic = true;
            else if (a == "--check-export")
                checkExport = true;
            else if (a == "--paper")
                paper = true;
            else if (a == "--smoke")
                smoke = true;
            else if (a == "--seed" && i + 1 < argc)
                seed = std::stoi(argv[++i]);
            else if (a == "--steps" && i + 1 < argc)
                steps = std::stoi(argv[++i]);
            else if (a == "--out" && i + 1 < argc)
                out = argv[++i];
            else
                throw std::runtime_error("Unknown/incomplete argument: " + a);
        }
        if (smoke)
        {
            steps = 4;
            batch = 32;
        }
        torch::set_num_threads(4);
        torch::manual_seed(seed);
        std::filesystem::create_directories(out);
        if (paper && dynamic)
            throw std::runtime_error("Paper benchmark uses static obstacles");
        System sys(paper, dynamic);
        if (smoke)
            sys.horizon = 12;
        Net h(sys.ed + 4, 64, 1), s(sys.ed + 4, 64, 2), tau(sys.ed, 32, 1);
        if (checkExport)
        {
            torch::load(h, out + "/barrier.pt");
            torch::load(s, out + "/switch.pt");
            torch::load(tau, out + "/threshold.pt");
            fixture(sys, h, s, tau, out);
            std::cout << "Export check fixture written" << std::endl;
            return 0;
        }
        std::ofstream log(out + "/loss.csv");
        log << "stage,iteration,loss\n";
        {
            auto x = torch::tensor({{0.f, 0.f, 1.f, -1.f}}), u = torch::tensor({{1.f, -1.f}});
            auto next = sys.step(x, u);
            if (std::abs(next[0][0].item<float>() - (sys.dt + .5f * sys.dt * sys.dt)) > 1e-6)
                throw std::runtime_error("Dynamics test failed");
            auto z = torch::tensor({{.2f, -.3f}}, torch::TensorOptions().requires_grad(true));
            auto soft = torch::tanh(z / .25);
            auto hard = torch::where(z >= 0, torch::ones_like(z), -torch::ones_like(z));
            auto st = hard.detach() - soft.detach() + soft;
            if (!torch::allclose(st, hard))
                throw std::runtime_error("Support point test failed");
            st.sum().backward();
            if (z.grad().abs().min().item<float>() <= 0)
                throw std::runtime_error("Straight-through test failed");
        }
        auto record = [&](const char *stage, int i, Tensor loss) {
            float v = loss.item<float>();
            if (!std::isfinite(v))
                throw std::runtime_error("Nonfinite loss");
            log << stage << "," << i << "," << v << "\n";
            if (i % 100 == 0)
                std::cout << stage << " " << i << " loss=" << v << std::endl;
        };
        torch::optim::Adam oh(h->parameters(), torch::optim::AdamOptions(.001));
        for (int i = 0; i < steps; i++)
        {
            auto x = sys.states(batch), e = sys.environments(batch);
            Tensor y;
            {
                torch::NoGradGuard no;
                y = sys.rollout(x, e, h, s, tau, 0, false, true);
            }
            auto loss = torch::smooth_l1_loss(h->forward(sys.features(x, e)).squeeze(1), y);
            oh.zero_grad();
            loss.backward();
            oh.step();
            record("A_barrier", i, loss);
        }
        for (auto &p : h->parameters())
            p.requires_grad_(false);
        torch::optim::Adam os(s->parameters(), torch::optim::AdamOptions(.001));
        for (int i = 0; i < std::max(4, steps / 4); i++)
        {
            auto x = sys.states(batch), e = sys.environments(batch);
            auto target = torch::sign(sys.brake(x));
            auto loss = torch::softplus(-target * s->forward(sys.features(x, e))).mean();
            os.zero_grad();
            loss.backward();
            os.step();
            record("B_pretrain", i, loss);
        }
        for (int i = 0; i < steps; i++)
        {
            auto x = sys.states(batch).requires_grad_(true), e = sys.environments(batch);
            auto hv = h->forward(sys.features(x, e));
            auto dh = torch::autograd::grad({hv.sum()}, {x})[0].detach();
            auto q =
                .5 * sys.dt * sys.dt * dh.index({Slice(), Slice(0, 2)}) + sys.dt * dh.index({Slice(), Slice(2, 4)});
            auto w = (q.norm(2, 1) > .01).to(torch::kFloat32);
            auto logits = s->forward(sys.features(x, e));
            auto align = (w * torch::softplus(-torch::sign(q) * logits).mean(1)).mean();
            auto smooth = (s->forward(sys.features(x + .01 * torch::randn_like(x), e)) - logits).square().mean();
            auto loss = -sys.rollout(x, e, h, s, tau, 2, true, true).mean() + .02 * align + .001 * smooth;
            os.zero_grad();
            loss.backward();
            os.step();
            record("B_switch", i, loss);
        }
        std::vector<Tensor> es, ys;
        {
            torch::NoGradGuard no;
            for (int j = 0; j < (smoke ? 8 : 128); j++)
            {
                auto e = sys.environments(1), x = sys.states(smoke ? 128 : 1024), ee = e.repeat({x.size(0), 1});
                auto safe = sys.rollout(x, ee, h, s, tau, 0, false, false) >= 0;
                auto values = h->forward(sys.features(x, ee)).squeeze(1).index({safe});
                if (values.numel())
                {
                    es.push_back(e);
                    ys.push_back(torch::quantile(values, .1).reshape({1, 1}));
                }
            }
        }
        if (es.empty())
            throw std::runtime_error("No brake-safe calibration samples");
        auto ec = torch::cat(es), yc = torch::cat(ys);
        torch::optim::Adam ot(tau->parameters(), torch::optim::AdamOptions(.001));
        for (int i = 0; i < steps; i++)
        {
            auto loss = torch::smooth_l1_loss(tau->forward(ec), yc);
            ot.zero_grad();
            loss.backward();
            ot.step();
            record("C_threshold", i, loss);
        }
        evaluate(sys, h, s, tau, seed, "staged", out, smoke ? 64 : 1024);
        std::vector<Tensor> ps = s->parameters();
        auto tp = tau->parameters();
        ps.insert(ps.end(), tp.begin(), tp.end());
        torch::optim::Adam oj(ps, torch::optim::AdamOptions(.0003));
        for (int i = 0; i < steps; i++)
        {
            auto x = sys.states(batch), e = sys.environments(batch);
            Tensor bs;
            {
                torch::NoGradGuard no;
                bs = (sys.rollout(x, e, h, s, tau, 0, false, false) >= 0).to(torch::kFloat32);
            }
            auto g = torch::sigmoid((tau->forward(e) - h->forward(sys.features(x, e))) / .1).squeeze(1);
            auto protect = (g * bs).mean() / (bs.mean() + 1e-6);
            auto loss = -sys.rollout(x, e, h, s, tau, 1, true, true).mean() + .01 * protect;
            oj.zero_grad();
            loss.backward();
            oj.step();
            record("D_joint", i, loss);
        }
        evaluate(sys, h, s, tau, seed, "joint", out, smoke ? 64 : 1024);
        torch::save(h, out + "/barrier.pt");
        torch::save(s, out + "/switch.pt");
        torch::save(tau, out + "/threshold.pt");
        std::ofstream json(out + "/model.json");
        json << std::setprecision(9) << "{\"dynamic\":" << (dynamic ? "true" : "false")
             << ",\"schema\":1,\"paper\":" << (paper ? "true" : "false") << ",\"smoke\":" << (smoke ? "true" : "false")
             << ",\"seed\":" << seed << ",\"dt\":" << sys.dt << ",\"horizon\":" << sys.horizon
             << ",\"robot_radius\":" << sys.radius << ",\"environment_dim\":" << sys.ed << ",\"barrier\":";
        netjson(json, h);
        json << ",\"switch\":";
        netjson(json, s);
        json << ",\"threshold\":";
        netjson(json, tau);
        json << "}";
        fixture(sys, h, s, tau, out);
        std::cout << "Finished: " << out << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
