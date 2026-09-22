Tetris consolidated branch status
================================

Checkpoint: 2026-09-22. The canonical branch of ``xxxvik-xakerxxx/u-boot``
is ``master``. Runtime source ``a55dc63bef`` includes the tested
``bf75c572e1`` SCP handoff without changing its hardware behavior.
Both ordinary CI ``35747798350`` and explicit SCP diagnostic CI
``35747862279`` passed. These new artifacts have not been flashed merely
for branch consolidation. See ``tetris-scp-loader.rst`` for exact installed
image identity, sensor evidence and the mandatory cold-start restrictions.

The native Linux display route/frame-end fixes are in the companion
``xxxvik-xakerxxx/nothing-tetris-pmaports`` main branch, not isolated in an
old display branch. Its ``docs/PORT_SUMMARY.md`` is the cross-repository
hardware and integration summary. Sensors remain Partial; working display
does not establish 120 Hz, GPU acceleration or complete suspend support.

Former remote branches are preserved as tags under
``archive/2026-09-22/<former-branch-name>`` before branch deletion:

* ``codex/scp-handoff-inventory``: ``e90badbdbc``, included in master.
* ``codex/b40-early-breadcrumb``: ``281058eab6``, unresolved early-boot work.
* ``codex/b40-fastboot-diagnostic``: ``5adc90565f``, earlier diagnostic.
* ``codex/ccci-prev-fdt-diagnostic``: ``1b8c954dba``, handoff diagnostic.
* ``lenowo``: ``51222dd867``, upstream snapshot included in master.
* ``ub-as-bl2``: ``0db380b6a8``, alternative upstream boot experiment.

Unique diagnostic histories are retained, not silently treated as verified
runtime fixes or blindly merged into master. In particular, the B4.0 USB
cycling report remains unresolved. No general NOS 4.0 compatibility is
claimed. Local worktrees and uncommitted research are not deleted.
