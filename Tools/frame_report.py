import argparse
import csv
import json
from pathlib import Path
import statistics


def percentiles(values):
    if not values:
        return None
    ordered = sorted(values)

    def at(fraction):
        index = (len(ordered) - 1) * fraction
        low = int(index)
        high = min(low + 1, len(ordered) - 1)
        return round(ordered[low] + (ordered[high] - ordered[low]) * (index - low), 3)

    return {"median": round(statistics.median(ordered), 3), "p95": at(0.95), "p99": at(0.99), "max": round(ordered[-1], 3)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--first-field", type=int, default=3000)
    parser.add_argument("--last-field", type=int, default=3900)
    args = parser.parse_args()
    root = args.capture
    first, last = args.first_field, args.last_field
    with (root / "frames.csv").open(newline="") as handle:
        frames = [row for row in csv.DictReader(handle) if first <= int(row["field"]) <= last]
    timeline = []
    for line in (root / "timeline.txt").read_text().splitlines():
        fields = line.split()
        if len(fields) >= 6 and fields[0].isdigit() and first <= int(fields[0]) <= last:
            timeline.append((int(fields[0]), float(fields[1]), int(fields[2], 16), int(fields[4], 16) | (int(fields[5], 16) << 32)))
    if len(frames) < 2 or len(timeline) < 2:
        parser.error("Capture did not reach this field window; inspect stdout and choose a gameplay window that exists")
    elapsed = float(frames[-1]["seconds"]) - float(frames[0]["seconds"])
    guest_elapsed = timeline[-1][1] - timeline[0][1]
    updates = (timeline[-1][2] - timeline[0][2]) & 0xFFFFFFFF
    clock_ticks = (timeline[-1][3] - timeline[0][3]) & 0xFFFFFFFFFFFFFFFF
    valid_hash = [row for row in frames if row["hash_valid"] == "1"]
    changed = [float(row["seconds"]) for row in valid_hash if row["image_changed"] == "1"]
    source_changes = [row for row in frames[1:] if row["new_source"] == "1"]
    source_times = [float(row["seconds"]) for row in source_changes]
    logic_gaps = []
    update_at = timeline[0][1]
    for previous, current in zip(timeline, timeline[1:]):
        if current[2] != previous[2]:
            logic_gaps.append((current[1] - update_at) * 1000)
            update_at = current[1]
    report = {
        "field_window": [first, last],
        "host_seconds": round(elapsed, 3),
        "emulated_vblanks_per_second": round((timeline[-1][0] - timeline[0][0]) / guest_elapsed, 3),
        "game_scheduler_updates_per_second": round(updates / guest_elapsed, 3),
        "game_clock_seconds_per_wall_second": round(clock_ticks / 147456000 / guest_elapsed, 5),
        "host_swap_calls_per_second": round((len(frames) - 1) / elapsed, 3),
        "completed_sources_delivered_per_second": round(len(source_changes) / elapsed, 3) if any(row["shared"] == "1" for row in frames) else None,
        "changed_images_per_second": round(sum(row["image_changed"] == "1" for row in valid_hash[1:]) / elapsed, 3) if len(valid_hash) == len(frames) else None,
        "repeated_image_swaps": sum(row["image_changed"] == "0" for row in valid_hash[1:]) if valid_hash else None,
        "host_swap_interval_ms": percentiles([float(row["interval_ms"]) for row in frames[1:]]),
        "changed_image_interval_ms": percentiles([(b - a) * 1000 for a, b in zip(changed, changed[1:])]),
        "completed_source_interval_ms": percentiles([(b - a) * 1000 for a, b in zip(source_times, source_times[1:])]),
        "observed_game_update_interval_ms": percentiles(logic_gaps),
        "host_submit_ms": percentiles([float(row["submit_ms"]) for row in frames]),
        "host_swap_call_ms": percentiles([float(row["swap_ms"]) for row in frames]),
        "notes": [
            "Host swaps are API returns, not physical display or photon timestamps.",
            "Completed sources are compositor outputs; they are not necessarily different images.",
            "Image change uses a 64-bit pixel fingerprint and dimensions; hash collisions remain possible.",
            "Image hashes add work and should be disabled for the primary performance comparison.",
            "Game update gaps are sampled at fields and cannot resolve multiple updates within one field.",
            "The two traces select the same field window; their first and last wall timestamps can differ.",
        ],
    }
    if (root / "resources.csv").exists():
        with (root / "resources.csv").open(newline="") as handle:
            samples = [row for row in csv.DictReader(handle) if timeline[0][1] <= float(row["absolute_seconds"]) <= timeline[-1][1]]
        if len(samples) >= 2:
            begin, finish = samples[0], samples[-1]
            duration = float(finish["absolute_seconds"]) - float(begin["absolute_seconds"])
            delta = lambda key: int(finish[key]) - int(begin[key])
            process_cpu = (delta("process_kernel") + delta("process_user")) / 10000000 / duration
            system_cpu = (delta("system_kernel") + delta("system_user") - delta("system_idle")) / 10000000 / duration
            report["cpu_cores_busy"] = {"game_process": round(process_cpu, 3), "other_processes": round(max(0, system_cpu - process_cpu), 3)}
    path = root / f"report-{first}-{last}.json"
    path.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
