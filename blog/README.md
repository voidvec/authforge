# Fulla Blog

Source for [fulla.dev/blog](https://fulla.dev/blog) — rendered by the
Docusaurus blog plugin (`website/docusaurus.config.js`, `path: '../blog'`).
Mirrors the docs governance model: **this directory is the single source**;
the site builds straight from it, no copies.

## Conventions

- **File name** = `YYYY-MM-DD-<slug>.md` (or `YYYY-MM-DD-<slug>/index.md` when
  the post has co-located images under `YYYY-MM-DD-<slug>/`).
- **Frontmatter**: `title`, `description`, `authors` (key from
  [`authors.yml`](authors.yml)), `tags`, and `draft: true` while writing.
  A post with `draft: true` is excluded from production builds and only
  visible in `docusaurus start --draft` — flip it to publish.
- **Language**: English-primary. The zh-CN translation of a post lives at
  `website/i18n/zh-CN/docusaurus-plugin-content-blog/<locale-dir>/<same-filename>`
  (same-PR dual write when it exists; untranslated posts simply do not appear
  under `/zh-CN`).
- **Images**: co-locate under the post's directory, reference relatively.
- **Numbers discipline**: every performance figure must be traceable to
  `benchmarks/competitors/results/COMPARISON.md` (or the raw JSONs under
  `benchmarks/results/`) and stated with its scope limits. No unscoped
  claims — see `docs-local/blog/` working notes for the launch-post red lines.

## Building locally

```bash
cd website && npm run start -- --draft   # dev server incl. drafts
cd website && npm run build              # production build (drafts excluded)
```
