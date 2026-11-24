// Offline contour tracing and bounded fitting for fixed 320x240 masks.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
struct P {
  double x, y;
};
using Ring = std::vector<P>;
const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
int vertex(int x, int y) { return y * 321 + x; }
std::vector<Ring> trace(const uint8_t *m) {
  std::vector<uint8_t> edge(321 * 241);
  auto white = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < 320 && y < 240 &&
           ((m[y * 40 + x / 8] >> (x % 8)) & 1);
  };
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 320; ++x)
      if (white(x, y)) {
        if (!white(x, y - 1))
          edge[vertex(x, y)] |= 1;
        if (!white(x + 1, y))
          edge[vertex(x + 1, y)] |= 2;
        if (!white(x, y + 1))
          edge[vertex(x + 1, y + 1)] |= 4;
        if (!white(x - 1, y))
          edge[vertex(x, y + 1)] |= 8;
      }
  std::vector<Ring> rings;
  for (int s = 0; s < int(edge.size()); ++s)
    while (edge[s]) {
      int x = s % 321, y = s / 321, dir = 0;
      while (!(edge[s] & (1 << dir)))
        ++dir;
      Ring ring;
      int steps = 0, firstdir = dir;
      do {
        ring.push_back({double(x), double(y)});
        edge[vertex(x, y)] &= ~(1 << dir);
        x += dx[dir];
        y += dy[dir];
        if (x == s % 321 && y == s / 321)
          break;
        int next = -1;
        for (int turn : {1, 0, 3, 2}) {
          int d = (dir + turn) % 4;
          if (edge[vertex(x, y)] & (1 << d)) {
            next = d;
            break;
          }
        }
        if (next < 0)
          throw std::runtime_error("open contour");
        dir = next;
        if (++steps > 320 * 240 * 4)
          throw std::runtime_error("loop");
      } while (true);
      Ring simple;
      for (size_t i = 0; i < ring.size(); ++i) {
        auto a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i],
             c = ring[(i + 1) % ring.size()];
        if ((b.x - a.x) * (c.y - b.y) != (b.y - a.y) * (c.x - b.x))
          simple.push_back(b);
      }
      if (simple.size() >= 3)
        rings.push_back(std::move(simple));
    }
  return rings;
}
double area(const Ring &r) {
  double a = 0;
  for (size_t i = 0; i < r.size(); ++i) {
    auto p = r[i], q = r[(i + 1) % r.size()];
    a += p.x * q.y - p.y * q.x;
  }
  return a * .5;
}
double distance(P p, P a, P b) {
  double x = b.x - a.x, y = b.y - a.y,
         t = ((p.x - a.x) * x + (p.y - a.y) * y) / (x * x + y * y);
  t = std::clamp(t, 0., 1.);
  return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
}

void word(std::ofstream &f, uint32_t v) { f.write((char *)&v, 4); }

// Minimum-link paths between the same two diameter anchors as the reference.
// Feasible shortcuts obey the same point-to-finite-segment distance bound.
std::vector<int> minlinks(const Ring &v, int begin, int end, double eps) {
  int n = end - begin + 1;
  double eps2 = eps * eps;
  std::vector<int> links(n, 1000000), parent(n, -1);
  std::vector<double> penalty(n, 1e100);
  links[0] = 0;
  penalty[0] = 0;
  for (int i = 0; i < n - 1; ++i) {
    if (parent[i] < 0 && i)
      continue;
    double lower = -1e100, upper = 1e100;
    bool have = false;
    P origin = v[begin + i];
    for (int j = i + 1; j < n; ++j) {
      P p = v[begin + j];
      double vx = p.x - origin.x, vy = p.y - origin.y, len2 = vx * vx + vy * vy,
             len = std::sqrt(len2), theta = std::atan2(vy, vx);
      bool possible = true;
      if (have) {
        double mid = (lower + upper) * .5;
        while (theta - mid > 3.141592653589793)
          theta -= 6.283185307179586;
        while (theta - mid < -3.141592653589793)
          theta += 6.283185307179586;
        possible = theta >= lower - 1e-12 && theta <= upper + 1e-12;
      }
      if (possible && len2 > 0 && links[i] + 1 <= links[j]) {
        double error = 0;
        bool valid = true;
        for (int k = i + 1; k < j; ++k) {
          auto q = v[begin + k];
          double dx = q.x - origin.x, dy = q.y - origin.y,
                 t = std::clamp((dx * vx + dy * vy) / len2, 0., 1.);
          double ex = dx - t * vx, ey = dy - t * vy, e = ex * ex + ey * ey;
          if (e > eps2 + 1e-9) {
            valid = false;
            break;
          }
          error += e;
        }
        if (valid) {
          int steps = links[i] + 1;
          double score = penalty[i] + error;
          if (steps < links[j] ||
              (steps == links[j] && score < penalty[j] - 1e-10)) {
            links[j] = steps;
            penalty[j] = score;
            parent[j] = i;
          }
        }
      }
      if (len > eps + 1e-12) {
        double alpha = std::asin(std::min(1., eps / len));
        if (!have) {
          lower = theta - alpha;
          upper = theta + alpha;
          have = true;
        } else {
          lower = std::max(lower, theta - alpha);
          upper = std::min(upper, theta + alpha);
          if (lower > upper + 1e-12)
            break;
        }
      }
    }
  }
  if (parent[n - 1] < 0)
    throw std::runtime_error("No finite shortcut path");
  std::vector<int> ids;
  for (int j = n - 1; j >= 0; j = parent[j]) {
    ids.push_back(begin + j);
    if (j == 0)
      break;
  }
  std::reverse(ids.begin(), ids.end());
  return ids;
}

int strategy = 0;
struct PolyFit {
  Ring raw, points;
  std::vector<int> ids;
};
PolyFit cyclic(const Ring &r, double eps) {
  int n = r.size();
  std::vector<int> starts{0};
  for (int axis = 0; axis < 2; ++axis)
    for (int sign : {-1, 1}) {
      int best = 0;
      for (int i = 1; i < n; ++i)
        if (sign * (axis ? r[i].y : r[i].x) >
            sign * (axis ? r[best].y : r[best].x))
          best = i;
      if (std::find(starts.begin(), starts.end(), best) == starts.end())
        starts.push_back(best);
    }
  PolyFit best;
  double bestPenalty = 1e100;
  for (int start : starts) {
    Ring v;
    for (int i = 0; i <= n; ++i)
      v.push_back(r[(start + i) % n]);
    auto ids = minlinks(v, 0, n, eps);
    ids.pop_back();
    if (ids.size() < 3)
      continue;
    Ring points;
    double penalty = 0;
    for (int i : ids)
      points.push_back(v[i]);
    if (area(points) * area(r) <= 0)
      continue;
    for (size_t j = 0; j < ids.size(); ++j) {
      int a = ids[j], b = j + 1 < ids.size() ? ids[j + 1] : n;
      for (int k = a + 1; k < b; ++k) {
        double d = distance(v[k], v[a], v[b]);
        penalty += d * d;
      }
    }
    if (best.ids.empty() || ids.size() < best.ids.size() ||
        (ids.size() == best.ids.size() && penalty < bestPenalty)) {
      v.pop_back();
      best = {v, points, ids};
      bestPenalty = penalty;
    }
  }
  if (best.ids.empty()) {
    std::vector<int> ids;
    for (int i = 0; i < n; ++i)
      ids.push_back(i);
    return {r, r, ids};
  }
  return best;
}
P rawpoint(const PolyFit &p, int i) {
  int n = p.raw.size();
  return p.raw[(i % n + n) % n];
}
struct Error {
  double maximum = 0, penalty = 0;
};
Error error(const PolyFit &p, int a, int b, P x, P y) {
  int n = p.raw.size();
  while (b <= a)
    b += n;
  Error out;
  if (std::hypot(y.x - x.x, y.y - x.y) < 1e-10)
    return {1e100, 1e100};
  for (int i = a; i <= b; ++i) {
    double d = distance(rawpoint(p, i), x, y);
    out.maximum = std::max(out.maximum, d);
  }
  for (int i = a; i < b; ++i) {
    P u = rawpoint(p, i), v = rawpoint(p, i + 1),
      m{(u.x + v.x) * .5, (u.y + v.y) * .5};
    double du = distance(u, x, y), dm = distance(m, x, y),
           dv = distance(v, x, y);
    out.penalty += std::hypot(v.x - u.x, v.y - u.y) *
                   (du * du + 4 * dm * dm + dv * dv) / 6.;
  }
  return out;
}
struct Line {
  double nx, ny, c;
};
Line fitline(const PolyFit &p, int a, int b) {
  int n = p.raw.size();
  while (b <= a)
    b += n;
  double weight = 0, sx = 0, sy = 0, xx = 0, xy = 0, yy = 0;
  for (int i = a; i < b; ++i) {
    P u = rawpoint(p, i), v = rawpoint(p, i + 1);
    double w = std::hypot(v.x - u.x, v.y - u.y);
    weight += w;
    sx += w * (u.x + v.x) * .5;
    sy += w * (u.y + v.y) * .5;
    xx += w * (u.x * u.x + u.x * v.x + v.x * v.x) / 3.;
    yy += w * (u.y * u.y + u.y * v.y + v.y * v.y) / 3.;
    xy += w * (2 * u.x * u.y + u.x * v.y + v.x * u.y + 2 * v.x * v.y) / 6.;
  }
  double mx = sx / weight, my = sy / weight,
         angle =
             .5 * std::atan2(2 * (xy / weight - mx * my),
                             xx / weight - mx * mx - (yy / weight - my * my));
  double nx = -std::sin(angle), ny = std::cos(angle);
  return {nx, ny, nx * mx + ny * my};
}
double motion_radius = .5;
P intersection(Line a, Line b, P old) {
  double det = a.nx * b.ny - a.ny * b.nx;
  if (std::abs(det) < 1e-7)
    return old;
  P v{(a.c * b.ny - a.ny * b.c) / det, (a.nx * b.c - a.c * b.nx) / det};
  v.x = std::clamp(v.x, old.x - motion_radius, old.x + motion_radius);
  v.y = std::clamp(v.y, old.y - motion_radius, old.y + motion_radius);
  v.x = std::round(v.x * 32) / 32;
  v.y = std::round(v.y * 32) / 32;
  return v;
}
double penalty(const PolyFit &p) {
  double sum = 0;
  for (size_t j = 0; j < p.ids.size(); ++j) {
    size_t k = (j + 1) % p.ids.size();
    sum += error(p, p.ids[j], p.ids[k], p.points[j], p.points[k]).penalty;
  }
  return sum;
}
bool valid(const PolyFit &p, double eps) {
  if (p.ids.size() < 3 || area(p.points) * area(p.raw) <= 0)
    return false;
  for (size_t j = 0; j < p.ids.size(); ++j) {
    size_t k = (j + 1) % p.ids.size();
    if (error(p, p.ids[j], p.ids[k], p.points[j], p.points[k]).maximum >
        eps + 1e-9)
      return false;
  }
  return true;
}
void adjust(PolyFit &p, double eps) {
  int n = p.ids.size();
  for (int pass = 0; pass < 2; ++pass)
    for (int j = 0; j < n; ++j) {
      int a = (j + n - 1) % n, b = (j + 1) % n;
      P anchor = rawpoint(p, p.ids[j]);
      P desired = intersection(fitline(p, p.ids[a], p.ids[j]),
                               fitline(p, p.ids[j], p.ids[b]), anchor);
      P old = p.points[j], best = old;
      double score = error(p, p.ids[a], p.ids[j], p.points[a], old).penalty +
                     error(p, p.ids[j], p.ids[b], old, p.points[b]).penalty;
      for (double t : {1., .5, .25}) {
        P v{std::round((old.x + (desired.x - old.x) * t) * 32) / 32,
            std::round((old.y + (desired.y - old.y) * t) * 32) / 32};
        auto e = error(p, p.ids[a], p.ids[j], p.points[a], v),
             f = error(p, p.ids[j], p.ids[b], v, p.points[b]);
        if (e.maximum <= eps + 1e-9 && f.maximum <= eps + 1e-9 &&
            e.penalty + f.penalty < score - 1e-9) {
          score = e.penalty + f.penalty;
          best = v;
        }
      }
      p.points[j] = best;
    }
}
void merge(PolyFit &p, double eps, double budget) {
  while (p.ids.size() > 3) {
    int n = p.ids.size(), chosen = -1;
    P newa{}, newc{};
    double current = penalty(p), best = 1e100;
    for (int j = 0; j < n; ++j) {
      int a = (j + n - 1) % n, c = (j + 1) % n, b = (j + n - 2) % n,
          d = (j + 2) % n;
      Line centre = fitline(p, p.ids[a], p.ids[c]);
      P pa = intersection(fitline(p, p.ids[b], p.ids[a]), centre,
                          rawpoint(p, p.ids[a])),
        pc = intersection(centre, fitline(p, p.ids[c], p.ids[d]),
                          rawpoint(p, p.ids[c]));
      auto e0 = error(p, p.ids[b], p.ids[a], p.points[b], pa),
           e1 = error(p, p.ids[a], p.ids[c], pa, pc),
           e2 = error(p, p.ids[c], p.ids[d], pc, p.points[d]);
      if (std::max({e0.maximum, e1.maximum, e2.maximum}) > eps + 1e-9)
        continue;
      double old =
          error(p, p.ids[b], p.ids[a], p.points[b], p.points[a]).penalty +
          error(p, p.ids[a], p.ids[j], p.points[a], p.points[j]).penalty +
          error(p, p.ids[j], p.ids[c], p.points[j], p.points[c]).penalty +
          error(p, p.ids[c], p.ids[d], p.points[c], p.points[d]).penalty;
      double score = current - old + e0.penalty + e1.penalty + e2.penalty;
      if (score > budget || score >= best)
        continue;
      PolyFit trial = p;
      trial.points[a] = pa;
      trial.points[c] = pc;
      trial.points.erase(trial.points.begin() + j);
      trial.ids.erase(trial.ids.begin() + j);
      if (!valid(trial, eps))
        continue;
      chosen = j;
      newa = pa;
      newc = pc;
      best = score;
    }
    if (chosen < 0)
      break;
    int a = (chosen + n - 1) % n, c = (chosen + 1) % n;
    p.points[a] = newa;
    p.points[c] = newc;
    p.points.erase(p.points.begin() + chosen);
    p.ids.erase(p.ids.begin() + chosen);
    if (!valid(p, eps))
      throw std::runtime_error("merge bound failure");
  }
}

Ring fitted(const Ring &r, double eps, double budget) {
  auto p = cyclic(r, eps);
  adjust(p, eps);
  merge(p, eps, budget);
  adjust(p, eps);
  if (!valid(p, eps))
    throw std::runtime_error("simplification error bound");
  return p.points;
}
// The reference pass uses the fitted 1.5-pixel contours.
Ring baseline_simplify(const Ring &r, double eps) {
  if (r.size() <= 4 || eps == 0)
    return r;
  return fitted(r, eps, 1e100);
}

PolyFit path_candidate(const Ring &r, int start, double coarse) {
  int n = r.size();
  Ring v;
  for (int i = 0; i <= n; ++i)
    v.push_back(r[(start + i) % n]);
  auto ids = minlinks(v, 0, n, coarse);
  ids.pop_back();
  v.pop_back();
  PolyFit p;
  p.raw = v;
  p.ids = ids;
  for (int i : ids)
    p.points.push_back(v[i]);
  return p;
}
void jointfit(PolyFit &p) {
  int n = p.ids.size();
  Ring next;
  for (int j = 0; j < n; ++j) {
    int a = (j + n - 1) % n, b = (j + 1) % n;
    next.push_back(intersection(fitline(p, p.ids[a], p.ids[j]),
                                fitline(p, p.ids[j], p.ids[b]),
                                rawpoint(p, p.ids[j])));
  }
  p.points = next;
}
bool repair(PolyFit &p, double eps) {
  if (p.ids.size() < 3)
    return false;
  int n = p.raw.size();
  for (int it = 0; it < n + 5; ++it) {
    jointfit(p);
    if (valid(p, eps))
      return true;
    int link = -1;
    double worst = eps;
    for (int j = 0; j < int(p.ids.size()); ++j) {
      int k = (j + 1) % p.ids.size();
      double e = error(p, p.ids[j], p.ids[k], p.points[j], p.points[k]).maximum;
      if (e > worst) {
        worst = e;
        link = j;
      }
    }
    if (link < 0)
      return false;
    int end = link + 1 < int(p.ids.size()) ? p.ids[link + 1] : n,
        begin = p.ids[link], at = -1;
    worst = -1;
    for (int i = begin + 1; i < end; ++i) {
      double e = distance(rawpoint(p, i), p.points[link],
                          p.points[(link + 1) % p.ids.size()]);
      if (e > worst) {
        worst = e;
        at = i;
      }
    }
    if (at < 0)
      return false;
    p.ids.insert(p.ids.begin() + link + 1, at);
    p.points.insert(p.points.begin() + link + 1, rawpoint(p, at));
  }
  return false;
}

void anchor_sweep(PolyFit &p, double eps) {
  int n = p.ids.size(), raw_n = p.raw.size();
  for (int pass = 0; pass < 2; ++pass)
    for (int j = 0; j < n; ++j) {
      int a = (j + n - 1) % n, c = (j + 1) % n;
      int left = p.ids[a], here = p.ids[j], right = p.ids[c];
      while (left >= here)
        left -= raw_n;
      while (right <= here)
        right += raw_n;
      double best = penalty(p);
      PolyFit winner = p;
      for (int delta : {-3, -2, -1, 1, 2, 3}) {
        int id = here + delta;
        if (id <= left || id >= right)
          continue;
        PolyFit trial = p;
        trial.ids[j] = (id % raw_n + raw_n) % raw_n;
        trial.points[j] =
            intersection(fitline(trial, trial.ids[a], trial.ids[j]),
                         fitline(trial, trial.ids[j], trial.ids[c]),
                         rawpoint(trial, trial.ids[j]));
        if (!valid(trial, eps))
          continue;
        double score = penalty(trial);
        if (score < best - 1e-7) {
          best = score;
          winner = std::move(trial);
        }
      }
      p = std::move(winner);
    }
}
void merge_blocks(PolyFit &p, double eps) {
  while (p.ids.size() > 3) {
    int n = p.ids.size(), most = 0;
    double score = 1e100;
    PolyFit winner;
    for (int count = 1; count <= 3 && n - count >= 3; ++count)
      for (int j = 0; j < n; ++j) {
        int a = (j + n - 1) % n, c = (j + count) % n, b = (a + n - 1) % n,
            d = (c + 1) % n;
        auto centre = fitline(p, p.ids[a], p.ids[c]);
        P pa = intersection(fitline(p, p.ids[b], p.ids[a]), centre,
                            rawpoint(p, p.ids[a])),
          pc = intersection(centre, fitline(p, p.ids[c], p.ids[d]),
                            rawpoint(p, p.ids[c]));
        PolyFit trial;
        trial.raw = p.raw;
        for (int k = 0; k < n; ++k) {
          bool remove = (k - j + n) % n < count;
          if (remove)
            continue;
          trial.ids.push_back(p.ids[k]);
          trial.points.push_back(k == a ? pa : k == c ? pc : p.points[k]);
        }
        if (!valid(trial, eps))
          continue;
        double e = penalty(trial);
        if (count > most || (count == most && e < score)) {
          most = count;
          score = e;
          winner = std::move(trial);
        }
      }
    if (!most)
      break;
    p = std::move(winner);
    adjust(p, eps);
  }
}

Ring search_simplify(const Ring &r, double eps) {
  if (r.size() <= 4 || eps == 0)
    return r;
  motion_radius = .5;
  auto base = cyclic(r, eps);
  adjust(base, eps);
  merge(base, eps, 1e100);
  adjust(base, eps);
  PolyFit best = base;
  double bestScore = penalty(best);
  int n = r.size();
  std::vector<int> starts{0};
  for (int axis = 0; axis < 2; ++axis)
    for (int sign : {-1, 1}) {
      int at = 0;
      for (int i = 1; i < n; ++i)
        if (sign * (axis ? r[i].y : r[i].x) > sign * (axis ? r[at].y : r[at].x))
          at = i;
      starts.push_back(at);
    }
  int samples = std::min(n, 20);
  for (int i = 0; i < samples; ++i)
    starts.push_back(i * n / samples);
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
  motion_radius = 1.;
  for (int start : starts)
    for (double factor : {1., 1.2, 1.4}) {
      auto p = path_candidate(r, start, eps * factor);
      if (p.ids.size() < 3 || p.ids.size() > best.ids.size() + 3)
        continue;
      if (factor > 1.) {
        if (!repair(p, eps))
          continue;
      } else {
        if (!valid(p, eps))
          continue;
        adjust(p, eps);
      }
      merge(p, eps, 1e100);
      adjust(p, eps);
      if (!valid(p, eps))
        continue;
      double score = penalty(p);
      if (p.ids.size() < best.ids.size() ||
          (p.ids.size() == best.ids.size() && score < bestScore)) {
        best = std::move(p);
        bestScore = score;
      }
    }
  {
    auto p = best;
    anchor_sweep(p, eps);
    merge_blocks(p, eps);
    anchor_sweep(p, eps);
    merge(p, eps, 1e100);
    adjust(p, eps);
    double score = penalty(p);
    if (valid(p, eps) &&
        (p.ids.size() < best.ids.size() ||
         (p.ids.size() == best.ids.size() && score < bestScore)))
      best = std::move(p);
  }
  motion_radius = .5;
  if (!valid(best, eps))
    throw std::runtime_error("search bound failure");
  return best.points;
}

bool protected_detail(const Ring &r) {
  double lx = 1e100, ly = 1e100, hx = -1e100, hy = -1e100;
  for (auto v : r) {
    lx = std::min(lx, v.x);
    ly = std::min(ly, v.y);
    hx = std::max(hx, v.x);
    hy = std::max(hy, v.y);
  }
  return std::max(hx - lx, hy - ly) <= 16 || std::abs(area(r)) <= 128 ||
         std::min(hx - lx, hy - ly) <= 3;
}
Ring exact_baseline(const Ring &r, double eps) {
  int saved = strategy;
  double movement = motion_radius;
  strategy = 2;
  motion_radius = .5;
  auto result = baseline_simplify(r, eps);
  strategy = saved;
  motion_radius = movement;
  return result;
}
// Only the three passes used by the accepted object are retained.
Ring simplify(const Ring &r, double eps) {
  if (strategy == 2)
    return baseline_simplify(r, eps);
  if (r.size() <= 4 || eps == 0)
    return r;
  if (protected_detail(r))
    return exact_baseline(r, std::min(eps, 1.5));
  int saved = strategy;
  strategy = 24;
  auto result = search_simplify(r, eps);
  strategy = saved;
  return result;
}
