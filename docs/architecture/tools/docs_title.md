<!-- generated documentation — edit the source, not this file -->
# `tools/docs_title.py`

Title the generated pages after the repository, not after the checkout directory.

Older page-generator releases took the project name from the basename of the
directory they ran in. In a linked worktree that name is the worktree
directory's, not the repository's, which put the wrong title on every page and
in the committed docs/ tree. The current release derives the name from git
itself, so this pass is a safety net that normally rewrites nothing.

The net only looks where a title can actually sit: the <title> tag, the
sidebar brand, and h1 headings in the rendered pages; the markdown H1 lines in
the two committed docs/ pages. It must not look anywhere else. A checkout can
be named after an ordinary word of the prose — a worktree named after, say,
the very thing this site is — and a blanket replacement would then rename that
word through running text, the generator's own ownership stamp (breaking its
regeneration), and the reference tree's rendered source listings. The
reference tree is excluded entirely: doxygen takes its project name from
docs/Doxyfile, never from the checkout.

The repository name comes from the common git directory, which every worktree
shares, so it is the same value from any checkout. When the two names agree
this is a no-op, which is the case in the main checkout.

Run from the repo root, after the generators and before the link pass.

## API

### `retitle(raw: bytes, slots, token: re.Pattern[bytes], repo: bytes) -> tuple[bytes, int]`
`tools/docs_title.py:52`

Substitute the checkout name inside title slots only; count edits.

**called by** `main`

<details><summary>Undocumented (3)</summary>

- `git`
- `fix`
- `main`

</details>
