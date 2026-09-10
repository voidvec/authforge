# CODEBUDDY.md

All agent guidance for this repository lives in [AGENTS.md](AGENTS.md) — the
cross-tool entry point indexing the authoritative rules, module guides, and
docs. Build commands, architecture patterns, critical rules, and test
conventions apply identically here. Path-scoped rule files under
`.claude/rules/` do NOT auto-load in CodeBuddy (that is Claude Code behavior) —
follow the AGENTS.md rule index and read the linked rule files when they apply
to files you are touching. CodeBuddy-native hard rules would live in
`.codebuddy/rules/<name>/RULE.mdc` (not created for this repo; AGENTS.md is
the single source). `git push` is forbidden for agents (human review
required).
