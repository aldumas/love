# Thread-heavy stress test, written primarily as a target for ThreadSanitizer
# (§E bug-hunt roster, row 4). The single-threaded scripts barely exercise the
# port's real concurrency; this one deliberately maximizes cross-thread overlap
# on the shared state TSan is meant to flag:
#
#   * Concurrent thread-VM boot. Each Love::Thread spins up its own mrb_state and
#     opens every Love module into it (g_threadVMOpener). That writes the GLOBAL,
#     process-wide registries in common/mrb_runtime.cpp (typeClasses, userData,
#     objectWrappers, moduleInstances), which are plain std::maps with no mutex.
#     Starting N workers in a tight loop makes those boots overlap -> the prime
#     data-race suspect.
#   * Shared named Channels. Producers/consumers hammer push / demand / pop /
#     peek / get_count / clear / perform_atomic on channels reached by name from
#     every VM, exercising Channel's own locking and the named-channel registry.
#   * Wrapper churn. Every get_channel / push / demand round-trips love::Objects
#     through mrbx_pushtype/pushvariant -> more concurrent objectWrappers/userData
#     traffic, from N VMs at once.
#
# Pass/fail here is a correctness backstop (all jobs processed, no thread error);
# the real signal is whether TSan reports a race while it runs.
#
#   ./love_mrb_harness thread_test.rb
#   make SANITIZE=thread BIN=$PWD/love_mrb_harness_tsan && \
#     DISPLAY=:1 ./love_mrb_harness_tsan thread_test.rb

T = Love::Thread

fail = 0
def check(label, got, want)
  ok = got == want
  puts "  #{ok ? 'ok ' : 'FAIL'} #{label}: #{got.inspect}#{ok ? '' : " (expected #{want.inspect})"}"
  ok
end

NWORKERS  = 8
NJOBS_PER = 40
TOTAL     = NWORKERS * NJOBS_PER

# Worker source (runs in its own VM). Reaches the shared channels by name, so no
# handle needs to cross the VM boundary. Each job creates a transient per-bucket
# scratch channel and does a perform_atomic read-modify-write on it, to keep the
# global wrapper maps churning under contention. A -1 job is the stop sentinel.
WORKER = <<~'RUBY'
  jobs    = Love::Thread.get_channel(name: "tsan.jobs")
  results = Love::Thread.get_channel(name: "tsan.results")
  loop do
    job = jobs.demand
    break if job == -1
    scratch = Love::Thread.get_channel(name: "tsan.scratch.#{job % 4}")
    scratch.push(value: job)
    scratch.perform_atomic { |c| c.get_count }
    scratch.peek
    scratch.pop
    results.push(value: job * 2)
  end
RUBY

jobs    = T.get_channel(name: "tsan.jobs")
results = T.get_channel(name: "tsan.results")

puts "=== spawning #{NWORKERS} workers x #{NJOBS_PER} jobs ==="

# Queue all the real jobs, then one stop sentinel per worker.
TOTAL.times { |i| jobs.push(value: i) }
NWORKERS.times { jobs.push(value: -1) }

# Start the workers as fast as possible so their VM boots overlap.
workers = []
NWORKERS.times do
  t = T.new_thread(code: WORKER)
  t.start
  workers << t
end

# Drain results concurrently with the workers. demand(timeout:) so a lost worker
# can't hang the suite -- a shortfall is reported as a failure instead.
sum = 0
got = 0
timed_out = false
while got < TOTAL
  r = results.demand(timeout: 10.0)
  if r.nil?
    timed_out = true
    break
  end
  sum += r
  got += 1
end

workers.each(&:wait)

puts "=== checks ==="
fail += 1 unless check("results collected", got, TOTAL)
fail += 1 if check("no demand timeout", timed_out, false) == false
expected_sum = (0...TOTAL).reduce(0) { |a, i| a + i * 2 }
fail += 1 unless check("sum of doubled jobs", sum, expected_sum)

# Every worker should have exited cleanly (a race-induced crash/exception would
# surface here as a non-empty error string).
errored = workers.map { |t| t.get_error }.compact
fail += 1 unless check("worker errors", errored, [])

# Channels should be drained: all jobs + sentinels consumed.
fail += 1 unless check("jobs channel empty", jobs.get_count, 0)
fail += 1 unless check("results channel empty", results.get_count, 0)

puts
if fail == 0
  puts "ALL PASS"
else
  puts "#{fail} CHECK(S) FAILED"
  exit 1
end
