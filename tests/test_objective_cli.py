"""CLI integration tests; fixtures and generated files stay in a temp directory."""
from pathlib import Path
import random
import re
import statistics
import subprocess
import tempfile
import unittest


EXE = Path(__file__).resolve().parents[1] / "exe/rMLGP"


def run(root, *args, success=True):
    result = subprocess.run([str(EXE), *map(str, args)], cwd=root,
                            capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout + result.stderr


def metric(edges, parts):
    groups = {}
    cut = 0
    for u, v, weight in edges:
        if parts[u - 1] != parts[v - 1]:
            cut += weight
            key = (u, parts[v - 1])
            groups[key] = max(groups.get(key, 0), weight)
    return cut, sum(groups.values())


def acyclic(edges, parts, k):
    successors = [set() for _ in range(k)]
    for u, v, _ in edges:
        if parts[u - 1] != parts[v - 1]:
            successors[parts[u - 1]].add(parts[v - 1])
    indegree = [sum(p in targets for targets in successors) for p in range(k)]
    ready = [p for p in range(k) if not indegree[p]]
    seen = 0
    while ready:
        p = ready.pop()
        seen += 1
        for q in successors[p]:
            indegree[q] -= 1
            if not indegree[q]:
                ready.append(q)
    return seen == k


with tempfile.TemporaryDirectory(prefix="dagp-objective-") as directory:
    root = Path(directory)
    rng = random.Random(19)
    n = 18
    edges = [(u, v, rng.randint(1, 9)) for u in range(1, n)
             for v in range(u + 1, n + 1) if rng.random() < 0.2]
    graph = root / "graph.dot"
    graph.write_text("digraph G {\n" + "".join(f"{v};\n" for v in range(1, n + 1))
                     + "".join(f"{u} -> {v} [weight={w}];\n" for u, v, w in edges)
                     + "}\n")
    common = ["--seed", "17", "--use_binary_input", "0", "--write_parts", "1",
              "--ratio", "1.5"]
    for invalid in ("-1", "2", "garbage", "1x", "", "99999999999999999999"):
        output = run(root, graph, 2, "--obj", invalid, success=False)
        assert "--obj must be 0" in output

    for invalid in ("0", "-1"):
        output = run(root, graph, 2, "--runs", invalid, success=False)
        assert "--runs must be positive" in output

    unit_exe = EXE.with_name("test_objective")
    for flag, message in (("--invalid-objective", "obj must be 0"),
                          ("--zero-runs", "parts and runs must be positive")):
        result = subprocess.run([str(unit_exe), flag], cwd=root,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0
        assert message in result.stdout + result.stderr

    for k in (1, 2, 3, 4):
        results = {}
        for objective in (None, 0, 1):
            args = [] if objective is None else ["--obj", objective]
            output = run(root, graph, k, *common, *args)
            assignment = Path(f"{graph}.partsfile.part_{k}.seed_17.txt")
            parts = list(map(int, assignment.read_text().split()))
            assert len(parts) == n
            assert all(0 <= p < k for p in parts)
            assert acyclic(edges, parts, k)
            cut, vol = metric(edges, parts)
            assert int(re.search(r"Edgecut: (\d+)", output)[1]) == cut
            assert int(re.search(r"Communication volume: (\d+)", output)[1]) == vol
            assert "Average Communication volume:" in output
            name = "communication volume" if objective == 1 else "edge cut"
            assert f"Objective: {name}\n" in output
            results[objective] = (parts, vol)
        assert results[None] == results[0]
        assert results[1][1] <= results[0][1]

    for objective in (None, 0, 1):
        args = [] if objective is None else ["--obj", objective]
        output = run(root, graph, 2, *common, *args, "--runs", 3,
                     "--inipart_nrun", 1, "--refinement", 0)
        values = list(map(int, re.findall(r"^Communication volume: (\d+)", output, re.M)))
        assert len(values) == 3
        cuts = list(map(int, re.findall(r"^\s*Edgecut: (\d+)", output, re.M)))
        # The fixture must exercise nonzero variance, not just three copies
        # of a deterministic topological-prefix cut.
        assert len(set(cuts)) > 1
        for label, scores in (("Average Communication volume: ", values),
                              ("Average Edgecut:", cuts)):
            assert len(scores) == 3
            summary = re.search(re.escape(label) + r"([\d.]+)\s+"
                                r"Standard Deviation: ([\d.]+)", output)
            mean, deviation = map(float, summary.groups())
            assert abs(mean - statistics.mean(scores)) < 0.001
            assert abs(deviation - statistics.pstdev(scores)) < 0.001

    # Exercise each volume neighborhood and the pass-disable path against
    # the same edge-cut seed, checking per-part violations independently.
    for k in (2, 3):
        for refinement, steps in [(mode, 10) for mode in range(5)] + [(1, 0)]:
            seeds = None
            for objective in (0, 1):
                output = run(root, graph, k, *common, "--obj", objective,
                             "--refinement", refinement, "--ref_step", steps)
                parts = list(map(int, Path(f"{graph}.partsfile.part_{k}.seed_17.txt")
                                 .read_text().split()))
                sizes = [parts.count(p) for p in range(k)]
                assert acyclic(edges, parts, k)
                cut, vol = metric(edges, parts)
                assert int(re.search(r"Edgecut: (\d+)", output)[1]) == cut
                assert int(re.search(r"Communication volume: (\d+)", output)[1]) == vol
                if objective == 0:
                    seeds = (parts, sizes, vol)
                else:
                    assert vol <= seeds[2]
                    for p in range(k):
                        assert min(1, seeds[1][p]) <= sizes[p] <= max(1.5*n/k, seeds[1][p])
                    if refinement == 0 or steps == 0:
                        assert parts == seeds[0]

    # Parallel edges make a legal input's cut exceed a signed 32-bit integer.
    large_edges = [(u, u + 1, 2000000000) for u in range(1, 8) for _ in range(2)]
    large = root / "large.dot"
    large.write_text("digraph G {\n" + "".join(f"{v};\n" for v in range(1, 9))
                     + "".join(f"{u} -> {v} [weight={w}];\n" for u, v, w in large_edges)
                     + "}\n")
    for objective in (0, 1):
        output = run(root, large, 2, *common, "--obj", objective)
        parts = list(map(int, Path(f"{large}.partsfile.part_2.seed_17.txt").read_text().split()))
        cut, vol = metric(large_edges, parts)
        assert cut > 2**31 - 1
        assert int(re.search(r"Edgecut: (\d+)", output)[1]) == cut
        assert int(re.search(r"Communication volume: (\d+)", output)[1]) == vol
        assert float(re.search(r"Average Edgecut:([\d.]+)", output)[1]) == cut
        assert acyclic(large_edges, parts, 2)

class ObjectiveRegressions(unittest.TestCase):
    def test_zero_cost_dependency(self):
        # A feasible four-way DAG. Zero-cost edges still impose dependencies
        # during seed generation, before the volume refiner is reached.
        weights = [4, 1, 3, 4]
        self.assertTrue(acyclic([(1, 3, 0)], [0, 1, 2, 3], 4))
        self.assertLessEqual(max(weights), 1.5 * sum(weights) / 4)
        with tempfile.TemporaryDirectory(prefix="dagp-zero-cost-") as directory:
            root = Path(directory)
            for cost in (1, 0):
                graph = root / f"dependency-{cost}.dot"
                edges = [(1, 3, cost)]
                graph.write_text("digraph G {\n" + "".join(
                    f"{v} [weight={w}];\n" for v, w in enumerate(weights, 1))
                    + f"1 -> 3 [weight={cost}];\n" + "}\n")
                for objective in (0, 1):
                    with self.subTest(cost=cost, objective=objective):
                        output = run(root, graph, 4, f"--obj={objective}",
                                     "--seed=17", "--ratio=1.5",
                                     "--use_binary_input=0", "--write_parts=1")
                        assignment = Path(f"{graph}.partsfile.part_4.seed_17.txt")
                        parts = list(map(int, assignment.read_text().split()))
                        self.assertEqual(len(parts), 4)
                        self.assertTrue(all(0 <= p < 4 for p in parts))
                        self.assertTrue(acyclic(edges, parts, 4))
                        cut, vol = metric(edges, parts)
                        self.assertEqual(int(re.search(r"Edgecut: (\d+)", output)[1]), cut)
                        self.assertEqual(int(re.search(
                            r"Communication volume: (\d+)", output)[1]), vol)

    def test_nonimproving_large_candidates(self):
        # Python integers let us describe both sides of the ecType boundary
        # without overflowing the test oracle itself. Every seed has volume
        # zero, so a correct refiner must leave it unchanged in every mode.
        max_cost = 2**63 - 1
        unit_exe = EXE.with_name("test_objective")
        with tempfile.TemporaryDirectory(prefix="dagp-large-candidate-") as directory:
            for weight in (max_cost // 2, max_cost // 2 + 1,
                           max_cost // 2 + 2, 5 * 10**18):
                for refinement in (1, 2, 3, 4):
                    with self.subTest(weight=weight, refinement=refinement,
                                      candidate_cost=2 * weight):
                        result = subprocess.run(
                            [str(unit_exe), "--large-candidate", str(refinement), str(weight)],
                            cwd=directory, capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
