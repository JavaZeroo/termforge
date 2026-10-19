# Working on TermForge

- Read README.md, docs/PLAN.md and the active milestone before making architecture decisions.
- The project is in research/baseline stage. Do not imply that a client exists or that parity has been measured.
- Do not lock the stack or add a large application skeleton before benchmark evidence supports it.
- Every performance claim needs workload identity, environment, correctness checks, raw samples and limitations.
- Separate observed facts, hypotheses and design decisions. Static symbols do not prove runtime behavior.
- Keep parser/screen state, history, transport and presentation independently testable in later implementation.
- Preserve terminal correctness and interactive latency while optimizing throughput or memory.
- Do not commit proprietary WindTerm artifacts, resources, private sessions, credentials, crash dumps or raw binary extraction dumps.
- Keep benchmark data local until reviewed for provenance and privacy. Publish synthetic workloads and sanitized results.
- Prefer focused changes with validation appropriate to the work. Do not invent measurements or percentage-complete estimates.
- Keep the roadmap and known differences current when scope changes.
- Documentation is primarily Chinese; technical identifiers and concise English summaries are welcome.
