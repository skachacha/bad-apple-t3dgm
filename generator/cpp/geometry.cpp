#include "../../third_party/earcut/earcut.hpp"
#include "contours.hpp"
#include <cstring>
using Point = std::array<double, 2>;
using Poly = std::vector<std::vector<Point>>;
bool inside(P p, const Ring &r) {
  bool in = false;
  for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
    auto a = r[i], b = r[j];
    if ((a.y > p.y) != (b.y > p.y) &&
        p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
      in = !in;
  }
  return in;
}
struct Mesh {
  std::vector<std::array<float, 2>> v;
  std::vector<uint32_t> tri;
  bool fallback = false;
};
Mesh triangulate(const std::vector<Ring> &raw, double eps) {
  std::vector<Ring> outer, holes;
  for (auto &r : raw)
    (area(r) > 0 ? outer : holes).push_back(r);
  std::vector<Poly> polys;
  for (auto &r : outer) {
    auto q = simplify(r, eps);
    Poly p(1);
    for (auto v : q)
      p[0].push_back({v.x, v.y});
    polys.push_back(p);
  }
  for (auto &r : holes) {
    P a = r[0], b = r[1];
    double len = std::hypot(b.x - a.x, b.y - a.y);
    P p{(a.x + b.x) * .5 + (b.y - a.y) * .01 / len,
        (a.y + b.y) * .5 - (b.x - a.x) * .01 / len};
    int target = -1;
    double best = 1e20;
    for (int i = 0; i < int(outer.size()); ++i)
      if (inside(p, outer[i]) && area(outer[i]) < best) {
        target = i;
        best = area(outer[i]);
      }
    if (target < 0)
      throw std::runtime_error("orphan hole");
    auto q = simplify(r, eps);
    std::vector<Point> h;
    for (auto v : q)
      h.push_back({v.x, v.y});
    polys[target].push_back(std::move(h));
  }
  Mesh result;
  std::map<std::pair<float, float>, uint32_t> seen;
  for (auto &p : polys) {
    auto indices = mapbox::earcut<uint32_t>(p);
    std::vector<Point> flat;
    for (auto &r : p)
      for (auto v : r)
        flat.push_back(v);
    double desired = 0;
    for (int j = 0; j < int(p.size()); ++j) {
      Ring q;
      for (auto v : p[j])
        q.push_back({v[0], v[1]});
      desired += (j ? -1 : 1) * std::abs(area(q));
    }
    double sum = 0;
    for (size_t j = 0; j < indices.size(); j += 3) {
      auto a = flat[indices[j]], b = flat[indices[j + 1]],
           c = flat[indices[j + 2]];
      sum += std::abs((b[0] - a[0]) * (c[1] - a[1]) -
                      (b[1] - a[1]) * (c[0] - a[0])) *
             .5;
    }
    if (std::abs(sum - desired) > 1e-5) {
      result.fallback = true;
      return result;
    }
    for (auto i : indices) {
      auto v = flat[i];
      auto key = std::make_pair(float(v[0]), float(v[1]));
      auto it = seen.find(key);
      if (it == seen.end()) {
        uint32_t n = result.v.size();
        seen.emplace(key, n);
        result.v.push_back({key.first, key.second});
        result.tri.push_back(n);
      } else
        result.tri.push_back(it->second);
    }
  }
  return result;
}
std::vector<uint8_t> raster(const Mesh &m) {
  std::vector<uint8_t> pixels(320 * 240);
  for (size_t i = 0; i < m.tri.size(); i += 3) {
    auto a = m.v[m.tri[i]], b = m.v[m.tri[i + 1]], c = m.v[m.tri[i + 2]];
    float signedarea =
        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (std::abs(signedarea) < 1e-6)
      continue;
    if (signedarea < 0)
      std::swap(b, c);
    int lx = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]})))),
        rx = std::min(319, int(std::ceil(std::max({a[0], b[0], c[0]}))));
    int ly = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]})))),
        ry = std::min(239, int(std::ceil(std::max({a[1], b[1], c[1]}))));
    for (int y = ly; y <= ry; ++y)
      for (int x = lx; x <= rx; ++x) {
        float px = x + .5f, py = y + .5f;
        auto cross = [&](auto p, auto q) {
          return (q[0] - p[0]) * (py - p[1]) - (q[1] - p[1]) * (px - p[0]);
        };
        if (cross(a, b) >= -1e-5 && cross(b, c) >= -1e-5 &&
            cross(c, a) >= -1e-5)
          pixels[y * 320 + x] = 1;
      }
  }
  return pixels;
}

void fit(const char *input, const std::string &profile, const char *output,
         const char *report_path) {
  if (profile == "reference")
    strategy = 2;
  else if (profile == "refined")
    strategy = 27;
  else if (profile == "final")
    strategy = 29;
  else
    throw std::runtime_error("Unknown fitting pass");
  double eps = profile == "final" ? 1.9 : 1.5;
  std::ifstream in(input, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot open masks");
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
  if (d.size() != size_t(8780) * 9600)
    throw std::runtime_error("Expected exactly 8780 packed 320x240 masks");
  std::ofstream out(output, std::ios::binary), report(report_path);
  if (!out || !report)
    throw std::runtime_error("Cannot create geometry output");
  word(out, 8780);
  word(out, 1);
  uint64_t nv = 0, nt = 0;
  int retries = 0;
  for (int f = 0; f < 8780; ++f) {
    auto raw = trace(d.data() + size_t(f) * 9600);
    Mesh mesh = triangulate(raw, eps);
    if (mesh.fallback && strategy >= 20) {
      int saved = strategy;
      strategy = 2;
      mesh = triangulate(raw, eps);
      strategy = saved;
    }
    if (mesh.fallback) {
      ++retries;
      for (double factor : {.75, .5, .25, 0.}) {
        mesh = triangulate(raw, eps * factor);
        if (!mesh.fallback)
          break;
      }
    }
    if (mesh.fallback || mesh.v.empty() && !raw.empty())
      throw std::runtime_error("Cannot triangulate picture");
    nv += mesh.v.size();
    nt += mesh.tri.size() / 3;
    word(out, f);
    word(out, mesh.v.size());
    word(out, mesh.tri.size() / 3);
    out.write((char *)mesh.v.data(), mesh.v.size() * 8);
    out.write((char *)mesh.tri.data(), mesh.tri.size() * 4);
    if (f % 100 == 0)
      std::cout << profile << " " << f << "/8780" << std::endl;
  }
  if (!out)
    throw std::runtime_error("Geometry output write failed");
  report << "{\"frames\":8780,\"vertices\":" << nv << ",\"triangles\":" << nt
         << ",\"triangulation_retries\":" << retries << "}\n";
}
void raster_file(const char *input, const char *output) {
  std::ifstream f(input, std::ios::binary);
  std::ofstream out(output, std::ios::binary);
  if (!f || !out)
    throw std::runtime_error("Cannot open raster files");
  uint32_t count = 0, stride = 0;
  f.read((char *)&count, 4);
  f.read((char *)&stride, 4);
  if (!f || count != 8780 || stride != 1)
    throw std::runtime_error("Invalid cache header");
  word(out, count);
  word(out, stride);
  for (uint32_t expected = 0; expected < count; ++expected) {
    uint32_t frame = 0, nv = 0, nt = 0;
    f.read((char *)&frame, 4);
    f.read((char *)&nv, 4);
    f.read((char *)&nt, 4);
    if (!f || frame != expected || nv > 65536 || nt > 1000000)
      throw std::runtime_error("Invalid mesh record");
    Mesh m;
    m.v.resize(nv);
    m.tri.resize(nt * 3);
    f.read((char *)m.v.data(), nv * 8);
    f.read((char *)m.tri.data(), nt * 12);
    if (!f)
      throw std::runtime_error("Short cache");
    for (auto i : m.tri)
      if (i >= nv)
        throw std::runtime_error("Invalid vertex index");
    auto pixels = raster(m);
    std::vector<uint8_t> b(9600);
    for (int i = 0; i < 76800; ++i)
      if (pixels[i])
        b[i / 8] |= 1 << (i % 8);
    word(out, frame);
    out.write((char *)b.data(), 9600);
  }
  if (f.peek() != EOF || !out)
    throw std::runtime_error("Unexpected cache tail or raster write failure");
}
int main(int argc, char **argv) {
  try {
    if (argc == 6 && std::string(argv[1]) == "fit")
      fit(argv[2], argv[3], argv[4], argv[5]);
    else if (argc == 4 && std::string(argv[1]) == "raster")
      raster_file(argv[2], argv[3]);
    else
      throw std::runtime_error("fit masks reference|refined|final cache report "
                               "OR raster cache masks");
    return 0;
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
