# Working on TermForge

- Read README.md, docs/PLAN.md, docs/FEATURES.md and the active milestone before making architecture decisions.
- The project has a limited Windows local-shell prototype. Do not claim a daily-use release, Linux/macOS client, complete WindTerm parity or measured performance equivalence.
- Do not lock the stack or add a large application skeleton before benchmark evidence supports it.
- Every performance claim needs workload identity, environment, correctness checks, raw samples and limitations.
- Separate observed facts, hypotheses and design decisions. Static symbols do not prove runtime behavior.
- Keep parser/screen state, history, transport and presentation independently testable in later implementation.
- Preserve terminal correctness and interactive latency while optimizing throughput or memory.
- Do not commit proprietary WindTerm artifacts, resources, private sessions, credentials, crash dumps or raw binary extraction dumps.
- Keep benchmark data local until reviewed for provenance and privacy. Publish synthetic workloads and sanitized results.
- Prefer focused changes with validation appropriate to the work. Do not invent measurements or percentage-complete estimates.
- Keep the roadmap and known differences current when scope changes.
- Follow docs/adr/0002-automation-first-investigation.md: prefer unattended CLI/test workflows; do not turn automation blockers into repeated user clicks, commands or per-run confirmations. Preserve unresolved evidence gaps and continue independent investigation/prototype work.
- R1–R5 in docs/PLAN.md are the current product milestones. Legacy M0–M4 and G1–G3 names appear only in historical evidence. Investigation and independently testable implementation may proceed in parallel; a complete baseline is required for supported performance claims.
- docs/FEATURES.md owns current capabilities and limits; docs/PLAN.md owns milestone scope; the active milestone owns acceptance contracts; GitHub issues own execution status and blockers. Do not append batch diaries to current planning documents.
- Start only an issue whose real blockers are resolved. Record source observations separately from TermForge policies, implement independently, and validate the complete user-visible behavior. An unknown target behavior is an investigation item, not a passed comparison.
- Before claiming common TUI support, complete ADR-0003's actual external-core comparison. Before product backend selection, revisit ADR-0004. Do not silently turn experimental choices into permanent architecture.
- Documentation is primarily Chinese; technical identifiers and concise English summaries are welcome.
