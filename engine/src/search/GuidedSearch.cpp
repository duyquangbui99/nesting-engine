#include "nest/Search.h"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <thread>
#include <unordered_map>

#include "nest/Refine.h"

#include "Placement.h"

namespace nest {
namespace {

using internal::Gene;
using internal::NestContext;
using Genome = std::vector<Gene>;
using Clock = std::chrono::steady_clock;

// One gene in ten is disturbed per child. High enough to keep exploring, low
// enough that a good ordering survives being bred.
constexpr double kMutationRate = 0.1;

// How a layout is judged, in the order section 5 of the spec sets out. Pieces
// that fit nowhere come first, since leaving one out fails the request
// outright. Then sheets, because material is the cost that matters. Then how
// tightly the last sheet is packed, so the offcut stays usable in one piece.
struct Fitness {
  int unplaced{0};
  int sheets{0};
  double occupiedArea{0.0};

  bool betterThan(const Fitness& other) const {
    if (unplaced != other.unplaced) return unplaced < other.unplaced;
    if (sheets != other.sheets) return sheets < other.sheets;
    return occupiedArea < other.occupiedArea;
  }
};

Fitness scoreOf(const NestResult& result) {
  return Fitness{result.unplaced, result.sheetCount,
                 result.occupied.valid() ? result.occupied.area() : 0.0};
}

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

int resolveThreadCount(int requested) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  // WebAssembly without SharedArrayBuffer has no threads at all, and spawning
  // one aborts rather than failing gracefully.
  (void)requested;
  return 1;
#else
  if (requested > 0) return std::min(requested, 16);
  const unsigned cores = std::thread::hardware_concurrency();
  if (cores <= 1) return 1;
  return static_cast<int>(std::min(cores - 1u, 16u));
#endif
}

// Identifies a gene across genomes, so crossover can tell which instances a
// child already carries.
std::uint64_t geneId(const Gene& gene) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(gene.designIndex))
          << 32) |
         static_cast<std::uint32_t>(gene.instanceIndex);
}

// Rewrites a genome that was placed with rotations left free into one that
// pins the rotations actually chosen, ordered as the pieces actually went
// down. Two things come of it. The genome becomes something crossover and
// mutation can work on, and every later evaluation costs a single rotation
// instead of all of them. In effect the search starts from what the greedy
// pass discovered and explores outwards from there.
Genome concretize(const NestResult& result, const NestContext& context,
                  const Genome& source) {
  std::unordered_map<std::uint64_t, int> chosen;
  chosen.reserve(result.placements.size());
  for (const Placement& p : result.placements) {
    const std::vector<double>& angles =
        context.angles[static_cast<std::size_t>(p.designIndex)];
    int index = 0;
    double closest = 360.0;
    for (std::size_t r = 0; r < angles.size(); ++r) {
      const double difference = std::abs(angles[r] - p.rotationDeg);
      if (difference < closest) {
        closest = difference;
        index = static_cast<int>(r);
      }
    }
    chosen[(static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.designIndex))
            << 32) |
           static_cast<std::uint32_t>(p.instanceIndex)] = index;
  }

  Genome placed;
  placed.reserve(source.size());
  for (const Placement& p : result.placements) {
    const std::uint64_t id =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.designIndex))
         << 32) |
        static_cast<std::uint32_t>(p.instanceIndex);
    placed.push_back(Gene{p.designIndex, p.instanceIndex, chosen[id]});
  }

  // Whatever did not fit keeps its place at the back, with a real rotation so
  // no gene is left free.
  for (const Gene& gene : source)
    if (!chosen.count(geneId(gene)))
      placed.push_back(Gene{gene.designIndex, gene.instanceIndex,
                            gene.rotationIndex < 0 ? 0 : gene.rotationIndex});
  return placed;
}

// The starting population.
//
// Genome zero pins every rotation, which makes it cheap, and it is evaluated on
// its own before anything else so a layout exists almost immediately. Genome
// one leaves rotations free, which reproduces the plain bottom-left pass
// exactly and is what stops the search ever coming out behind it; it costs as
// many placements as there are angles, so there is exactly one of it. The next
// two are orderings that are good guesses for any nest, and the rest are random
// to spread the population out.
std::vector<Genome> seedPopulation(const NestRequest& request,
                                   const NestContext& context, int size,
                                   std::mt19937& rng) {
  Genome natural;
  for (std::size_t d = 0; d < request.designs.size(); ++d)
    for (int i = 0; i < request.designs[d].quantity; ++i)
      natural.push_back(Gene{static_cast<int>(d), i, 0});

  Genome greedy = natural;
  for (Gene& gene : greedy) gene.rotationIndex = -1;

  const auto designArea = [&](const Gene& g) {
    return context.designArea[static_cast<std::size_t>(g.designIndex)];
  };
  const auto longestSide = [&](const Gene& g) {
    const Rect box =
        bounds(request.designs[static_cast<std::size_t>(g.designIndex)].outline);
    return std::max(box.width(), box.height());
  };

  Genome byArea = natural;
  std::stable_sort(byArea.begin(), byArea.end(),
                   [&](const Gene& a, const Gene& b) {
                     return designArea(a) > designArea(b);
                   });

  Genome bySide = natural;
  std::stable_sort(bySide.begin(), bySide.end(),
                   [&](const Gene& a, const Gene& b) {
                     return longestSide(a) > longestSide(b);
                   });

  std::vector<Genome> population{natural, greedy, byArea, bySide};
  population.resize(static_cast<std::size_t>(std::max(size, 4)));

  for (std::size_t i = 4; i < population.size(); ++i) {
    Genome g = natural;
    std::shuffle(g.begin(), g.end(), rng);
    for (Gene& gene : g) {
      const std::size_t count =
          context.shapes[static_cast<std::size_t>(gene.designIndex)].size();
      gene.rotationIndex = static_cast<int>(
          std::uniform_int_distribution<std::size_t>(0, count - 1)(rng));
    }
    population[i] = std::move(g);
  }
  return population;
}

// Order crossover: take a prefix of one parent, then fill from the other in its
// own order, skipping instances already taken. Each gene keeps the rotation it
// had in the parent it came from, so orderings and angles evolve together.
Genome crossover(const Genome& a, const Genome& b, std::mt19937& rng) {
  if (a.empty()) return a;
  const std::size_t cut =
      std::uniform_int_distribution<std::size_t>(1, a.size())(rng);

  Genome child(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(cut));
  std::unordered_map<std::uint64_t, bool> taken;
  taken.reserve(a.size());
  for (const Gene& gene : child) taken[geneId(gene)] = true;

  for (const Gene& gene : b)
    if (!taken.count(geneId(gene))) child.push_back(gene);
  return child;
}

void mutate(Genome& genome, const NestContext& context, std::mt19937& rng) {
  std::uniform_real_distribution<double> chance(0.0, 1.0);
  for (std::size_t i = 0; i < genome.size(); ++i) {
    if (chance(rng) < kMutationRate && i + 1 < genome.size())
      std::swap(genome[i], genome[i + 1]);
    if (chance(rng) < kMutationRate) {
      const std::size_t count =
          context.shapes[static_cast<std::size_t>(genome[i].designIndex)].size();
      genome[i].rotationIndex = static_cast<int>(
          std::uniform_int_distribution<std::size_t>(0, count - 1)(rng));
    }
  }
}

// Picks a parent, favouring the front of an already-sorted population. Squaring
// a uniform draw biases towards rank zero without ever excluding the tail.
std::size_t pickParent(std::size_t populationSize, std::mt19937& rng) {
  const double r = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
  const std::size_t index = static_cast<std::size_t>(r * r * populationSize);
  return std::min(index, populationSize - 1);
}

}  // namespace

SearchResult guidedSearch(const NestRequest& request,
                          const SearchOptions& options,
                          const std::atomic<bool>* cancel) {
  SearchResult search;
  const Clock::time_point start = Clock::now();

  const NestContext context = internal::buildContext(request);
  NfpCache cache;

  if (!context.valid || request.designs.empty()) {
    search.best = bottomLeftFill(request);
    search.elapsedSeconds = secondsSince(start);
    return search;
  }

  std::mt19937 rng(options.seed);
  const int populationSize = std::max(options.populationSize, 4);
  std::vector<Genome> population =
      seedPopulation(request, context, populationSize, rng);

  // Everything stops on this: the budget running out, the caller cancelling, or
  // a worker noticing either. Placement polls it between pieces.
  std::atomic<bool> stop{false};

  const auto outOfTime = [&] {
    return secondsSince(start) >= options.timeBudgetSeconds;
  };
  const auto after = [&](double seconds) {
    return start + std::chrono::duration_cast<Clock::duration>(
                       std::chrono::duration<double>(seconds));
  };
  // Handed to every placement so the budget bounds the work inside one
  // evaluation, not merely the number of evaluations started.
  const Clock::time_point deadline = after(options.timeBudgetSeconds);

  // The first layout gets longer, because a partial one is worse than a slow
  // one: reporting two of seven placed when the answer is seven reads as a
  // failure rather than as a search that ran out of time. Seven distinct
  // designs need a no-fit polygon per pair before a single piece lands, which
  // on real artwork is seconds of work that cannot be skipped. Still bounded,
  // so a pathological file cannot hang the caller.
  const Clock::time_point firstDeadline =
      after(std::max(options.timeBudgetSeconds * 4.0, 30.0));
  const auto shouldStop = [&] {
    if (stop.load(std::memory_order_relaxed)) return true;
    if (cancel && cancel->load(std::memory_order_relaxed)) {
      stop.store(true, std::memory_order_relaxed);
      return true;
    }
    if (outOfTime()) {
      stop.store(true, std::memory_order_relaxed);
      return true;
    }
    return false;
  };

  // The first seed is evaluated alone so a valid layout exists almost at once,
  // whatever happens to the rest of the budget.
  // Watched the same way, so an enormous job cannot blow through the budget
  // before the population has even started.
  const int threadCount = resolveThreadCount(options.threads);

  NestResult best;
  if (threadCount <= 1) {
    // Single threaded, which is what WebAssembly gets and what a one-core
    // machine gets. Nothing watches the clock here, so the first layout can
    // overshoot the budget; it is one pass and the alternative is no layout.
    best = internal::placeSequence(request, context, population[0], cache, &stop,
                                   firstDeadline);
  } else {
    std::atomic<int> running{1};
    std::thread first([&] {
      best = internal::placeSequence(request, context, population[0], cache,
                                     &stop, firstDeadline);
      running.fetch_sub(1, std::memory_order_release);
    });
    while (running.load(std::memory_order_acquire) > 0) {
      if (cancel && cancel->load(std::memory_order_relaxed))
        stop.store(true, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    first.join();
  }
  Fitness bestFitness = scoreOf(best);
  population[0] = concretize(best, context, population[0]);

  // A tiling costs milliseconds and, for a sheet of one design, sometimes beats
  // anything the population reaches inside the budget. It enters as both an
  // answer to beat and an ordering to breed from.
  if (options.useLattice) {
    const NestResult tiled =
        internal::latticeFill(request, context, cache, options.lattice);
    if (!tiled.placements.empty()) {
      ++search.evaluations;
      if (scoreOf(tiled).betterThan(bestFitness)) {
        best = tiled;
        bestFitness = scoreOf(best);
        search.bestFromLattice = true;
        if (options.onImprovement) options.onImprovement(best);
      }
      if (population.size() > 2)
        population[2] = concretize(tiled, context, population[2]);
    }
  }
  search.evaluations = 1;
  search.firstLayoutSeconds = secondsSince(start);
  if (options.onImprovement) options.onImprovement(best);

  search.threadsUsed = threadCount;

  while (!shouldStop()) {
    if (options.maxGenerations > 0 &&
        search.generations >= options.maxGenerations)
      break;

    std::vector<NestResult> results(population.size());
    std::vector<char> evaluated(population.size(), 0);
    std::atomic<std::size_t> next{0};

    const auto work = [&] {
      while (true) {
        if (stop.load(std::memory_order_relaxed)) return;
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= population.size()) return;
        if (cancel && cancel->load(std::memory_order_relaxed)) {
          stop.store(true, std::memory_order_relaxed);
          return;
        }
        if (outOfTime()) {
          stop.store(true, std::memory_order_relaxed);
          return;
        }
        results[i] = internal::placeSequence(request, context, population[i],
                                            cache, &stop, deadline);
        evaluated[i] = 1;
      }
    };

    if (threadCount <= 1) {
      // The work loop already checks the clock before picking up each genome,
      // which is all the budget enforcement there can be without a second
      // thread to watch it.
      work();
    } else {
      // The caller's thread watches the clock instead of doing a share of the
      // work. Without it nothing enforces the budget once every worker is busy:
      // they only look at the time before picking up the next genome, so a
      // generation of 500-piece layouts ran a two second budget for seven
      // seconds. Placement polls the same flag between pieces, so setting it
      // here stops the search within about one piece.
      std::atomic<int> running{threadCount};
      std::vector<std::thread> workers;
      workers.reserve(static_cast<std::size_t>(threadCount));
      for (int t = 0; t < threadCount; ++t)
        workers.emplace_back([&] {
          work();
          running.fetch_sub(1, std::memory_order_release);
        });

      while (running.load(std::memory_order_acquire) > 0) {
        if (outOfTime() || (cancel && cancel->load(std::memory_order_relaxed))) {
          stop.store(true, std::memory_order_relaxed);
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      for (std::thread& worker : workers) worker.join();
    }

    ++search.generations;

    // A genome evaluated with free rotations is rewritten to pin what it
    // chose, so it breeds like any other and costs one rotation next time.
    for (std::size_t i = 0; i < population.size(); ++i) {
      if (!evaluated[i]) continue;
      const bool anyFree =
          std::any_of(population[i].begin(), population[i].end(),
                      [](const Gene& g) { return g.rotationIndex < 0; });
      if (anyFree) population[i] = concretize(results[i], context, population[i]);
    }

    // Ranked by fitness, best first. Ties break on index so the ordering does
    // not depend on how the work happened to be shared out.
    std::vector<std::size_t> ranked;
    for (std::size_t i = 0; i < population.size(); ++i)
      if (evaluated[i]) {
        ranked.push_back(i);
        ++search.evaluations;
      }
    if (ranked.empty()) break;

    std::sort(ranked.begin(), ranked.end(),
              [&](std::size_t a, std::size_t b) {
                const Fitness fa = scoreOf(results[a]);
                const Fitness fb = scoreOf(results[b]);
                if (fa.betterThan(fb)) return true;
                if (fb.betterThan(fa)) return false;
                return a < b;
              });

    if (scoreOf(results[ranked.front()]).betterThan(bestFitness)) {
      best = results[ranked.front()];
      bestFitness = scoreOf(best);
      search.bestFromLattice = false;
      if (options.onImprovement) options.onImprovement(best);
    }

    if (shouldStop()) break;

    // Breed the next generation, keeping the two best unchanged so progress is
    // never lost.
    std::vector<Genome> survivors;
    survivors.reserve(ranked.size());
    for (std::size_t index : ranked) survivors.push_back(population[index]);

    std::vector<Genome> nextGeneration;
    nextGeneration.reserve(population.size());
    nextGeneration.push_back(survivors[0]);
    if (survivors.size() > 1) nextGeneration.push_back(survivors[1]);

    while (nextGeneration.size() < population.size()) {
      const Genome& mother = survivors[pickParent(survivors.size(), rng)];
      const Genome& father = survivors[pickParent(survivors.size(), rng)];
      Genome child = crossover(mother, father, rng);
      mutate(child, context, rng);
      nextGeneration.push_back(std::move(child));
    }
    population = std::move(nextGeneration);
  }

  // Refinement is applied once, to the answer. It never loosens a layout, so
  // its result always stands. Running it inside every evaluation would cost
  // more time than the quality is worth: measured on a coarse pass it moves the
  // occupied box by under a percent.
  // Read once at the end rather than wherever the stop was first noticed: a
  // worker usually sees the flag before the main thread does, and setting it
  // there would leave the caller unable to tell a cancellation from a budget
  // running out.
  if (cancel) search.cancelled = cancel->load(std::memory_order_relaxed);

  search.best = refine(request, best);
  search.best.nfpCacheHits = cache.hits();
  search.best.nfpCacheMisses = cache.misses();
  search.elapsedSeconds = secondsSince(start);
  return search;
}

}  // namespace nest
