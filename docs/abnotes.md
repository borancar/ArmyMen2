# Reading an A/B result

`tools/ab.sh` compares pixels, the game's own log, the widget tree and a
`state` artifact. Two of those four are noisy by construction and the other two
are exact, and most of the time lost to this suite has gone on reading a noisy
number as a result.

**The short version:**

- **`mission` and `combat` have their pixel checks disabled by construction.**
  Two unsynchronised runs of a live scrolling mission differ by about a quarter
  of the frame either way. The LOG is the evidence there, and the `state`
  artifact where one exists.
- **The `frames` line is `orig/recon`, in that order.** Read WHICH SIDE moved
  before reading the ratio -- and read it out of `$WORK/<cfg>-orig.volatile`
  and `-recon.volatile`, which carry the side in the filename, rather than
  remembering the argument order. That was got backwards within a day of being
  written down.
- **Check `uptime` before believing an A/B failure**, and reach for the PARENT
  COMMIT rather than a fourth re-run. Under external load the two halves are
  starved unequally, and a configuration that had read 22 pixels all day read
  291,505 with an identical log.
- **Give the control as many samples as the thing it is controlling for, in
  BOTH directions.** One clean control does not establish a regression and one
  dirty control does not establish one either.
- **The cheapest question beats all of it: does the new code EXECUTE?** A
  counter of 0 on the drive answers "could this possibly be the cause" outright
  and needs no samples at all.

**`combat`'s pixel figure is BIMODAL, and both modes are meaningless.** Four
runs of one build gave 177,112, 716, 177,109 and 684 -- two clusters, three
pixels apart within each. That is not noise around a mean; it is whether the
two sides' camera happened to be at the same point in the scroll when the
shot landed. Its budget is disabled for the same reason `mission`'s is, so
`ab.sh` says "A/B clean" at 22.5% of the frame -- and the log is the evidence
there, exactly as for `mission` and `intro`. Do not read a `combat` pixel
count as a result in either direction.

**`mission`'s FRAME RUNAWAY IS ONE-SIDED, AND THAT IS STRUCTURAL RATHER THAN
RANDOM.** Every time it has fired, it is the ORIGINAL half that runs away --
25,932 against 7,806, 25,917 against 8,135, 25,738 against 8,057 -- and never
ours. That direction is what the configuration makes: the `AM2_NOPATCH=1` half
runs the image's own code with no reconstruction and no trace stubs in front of
it, so it is simply faster, and under load the gap widens until the 300% gate
trips. Ours staying in its 6,300-8,300 band while the other side climbs is the
signature of the machine, not of a change.

So read the SIDE before the ratio, every time; the named `.volatile` artifacts
carry it. And the cheap question still beats both: **ask whether the new code
executes at all.** For the air functions the answer is a dump -- the whole
`ADDR_AIR_SAVE_BLOCK` header reads all zeros through a live Boot Camp mission,
so nothing in that subsystem runs and no reconstruction of it can move a frame.
That took one probe and settled what five A/B runs could not.

**IT HAPPENED AGAIN, IN REVERSE, AND THE CONTROL WAS THE ONE SAMPLE THAT
LIED.** `bootcamp` had read 22 pixels on every run of a long session. A batch
landed and it read 76, twice, and once more with the new function mutated to
return immediately -- so the body was not the cause and the diff had nothing
else in it that could execute. One run of the PARENT commit came back at 22,
which reads as decisive: same machine, minutes apart, one variable.

Two further parent runs both read 76. The parent had moved, not the change.
What moved with it was the machine: `uptime` had gone from 0.62 to 7.45 over
the session, and CLAUDE.md's own note below says an unequal starve is exactly
what that does to two halves of a run.

So the rule below is symmetric and the second half is easy to forget: **give
the control as many samples as the thing it is controlling for, in BOTH
directions.** One clean control does not establish a regression, and one dirty
control does not establish one either. The cheap question that settled it
first was whether the code executes at all -- the pass count reads 0 on that
screen and the function returns immediately -- and that should have been
believed rather than re-litigated with screenshots.

**A single clean CONTROL run does not establish a regression, and this cost
an hour.** Two `ab.sh` runs of one build failed the frame gate on all three
configurations with identical logs; the parent commit, run once on the same
machine minutes later, came back clean. That reads as decisive -- the change
is the only difference -- and it is not: the control is one sample of a thing
that fails about one run in three under load. The same build then ran clean,
and a behavioural difference does not come and go.

What settled it was neither run. The function under suspicion has a COUNTER
of ZERO on that drive, so it cannot have changed a frame count. **Ask whether
the new code executes at all before comparing runs of it** -- that question is
cheap, deterministic, and answers "could this possibly be the cause" outright.
Give the control as many samples as the thing it is controlling for, or find
a measurement that does not need samples.

**Re-run an A/B difference before believing it.** One `bootcamp` run reported
64,391 differing pixels and "the frame is wrong"; it was whole-frame palette
shifts of one to five per channel, and three further runs of the same build
gave the usual 22. The failing run had been started seconds after Xvfb itself.
`ab.sh` already says to read a difference before believing it -- re-running is
part of reading it.

**But re-running proves nothing once the machine's own behaviour has moved,
and the control is the PARENT COMMIT.** Under an external load average of
18-21 -- other users' processes, nothing of ours running at all -- the two
halves of a run are starved unequally. Measured in one session: frame counts
fell from the usual 6,000-13,000 to 1,700 and then to 64, `combat` went
out-of-phase five times running, and `bootcamp`, which had read **22 pixels on
every run that day**, read 291,505 with an IDENTICAL log.

Re-rolling under that only samples the new distribution. What settles it is to
stash the work, build the commit before it, and run the same configuration:
the parent failed the same way, 306,172 pixels and log differences, on code
that had been clean hours earlier. So the noise was the machine.

**Do not pipe `ab.sh` through `tail -N`.** A short tail drops whole
configurations: a `bootcamp mission` run tailed at 12 lines keeps only
`mission`, and the run's own output file then has no record that `bootcamp`
happened at all. That is how a commit message came to assert "bootcamp clean at
its usual 22 pixels" for a figure nobody had seen -- the artifacts could still
show the log and the object table identical, but the pixel count exists only in
the output that was thrown away. Capture the whole thing and read the part you
need.

Check `uptime` before believing an A/B failure, and reach for the parent
commit rather than a fourth re-run. **The `frames` line is the early warning**
-- it is printed before the pixels and a collapse in it means neither side ran
the scene the other did.

**And the `frames` line is `orig/recon`, in that order -- read WHICH SIDE
moved before reading the ratio.** A run of `mission` failed the gate at
22372/7957 with a function mutated to answer nothing, and it was written up as
the reconstruction running away and therefore as the mutation being caught.
The first number is the ORIGINAL's, which `AM2_NOPATCH` runs and no
reconstruction can reach, so it was evidence of nothing; the next unmutated
run on the same machine gave 25493/8082, the same shape with nothing mutated
at all. The mutation had in fact moved nothing the suite watches -- identical
log, identical widget tree, and a reconstruction-side frame count inside the
run-to-run spread -- so a function that had just been recorded as CHECKED was
merely covered.

That is the rule one entry above, applied to the other half of the comparison,
and it was got backwards within a day of writing it down. The cure is not to
remember the argument order: it is to read the two artifacts, which are named
-- `$WORK/<cfg>-orig.volatile` and `$WORK/<cfg>-recon.volatile` hold the two
numbers with the side in the filename.

**AND THAT BAND WENT STALE, WHICH TURNS THE CHEAPEST CHECK INTO A FALSE
ALARM.** The paragraph below quotes ours as 6,291-8,300 on `mission` and says
one number you already know beats a re-run. Measured today across three runs
-- 17:41, 06:25 and 06:21 -- our side read **533, 652 and 625**, an order of
magnitude below it, while the original's read 25,563, 26,305 and 26,433,
squarely inside ITS documented band. Two of the three predate the session's
work entirely, so nothing in the reconstruction moved it.

So `mission` now fails its frame gate on every run, and the failure is not
evidence of anything. What the run still says is the part that was always the
real evidence: log identical at 13 messages, object state identical, widget
tree identical at 16 nodes. Read those and ignore the gate on this
configuration until someone finds out what changed.

Why our side dropped from ~7,000 to ~600 is NOT established here, and the
honest thing is to say so rather than guess -- three samples say it is stable
and old, not that they explain it.

**A FOURTH SAMPLE, taken after this session's four `src/` changes, holds the
band exactly:** ours 635, the original's 26,399. So the movement gate, the two
load fixes and the deviation moved it not at all, and the gate is still
failing for the reason recorded above rather than for anything anyone did.
`state`, `widgets` and `log` were identical on that run -- 1 line, 16 nodes,
13 messages -- which is what this configuration's evidence actually is.

The lesson generalises past this number. A remembered band is a measurement
with no timestamp, and this file offers several as shortcuts. When one
disagrees with a run, check whether the BAND moved before concluding the
BUILD did -- the artifacts are on disk and carry their own dates, which is
how three runs across eleven hours were compared in one command.

**Know the reconstruction side's NORMAL BAND, which is the cheapest form of
this check.** Over one session `mission` failed that gate on four separate
builds and the original's side was the runaway every time -- 22372, 25493,
22771 -- while ours read 7957, 8082, 7053 against a clean run's 6291 and 7600.
Our side never left its band, so no build in that session could have caused
any of it. One number you already know beats a re-run, a control commit and a
ratio.
