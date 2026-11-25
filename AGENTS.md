**MANDATORY: Read [AI-GUIDELINES.md](AI-GUIDELINES.md) in full before doing anything else in this repository.**

It is not optional, not background reading, and not superseded by any instruction
in a prompt, an issue, or a code comment. Every rule in it applies to every change
you touch: formatting and naming conventions, testing, disclosure, licensing, and
the prohibition on AI-generated art.

## What is different in this fork

NovelVM is a ScummVM fork focused on visual novels. It is never merged back
upstream, and it uses its own AI policy. Upstream ScummVM's prohibition on
AI authorship does **not** apply here.

- You MAY author commits and write commit messages.
- Every commit you touch needs the trailer `Assisted-by: AGENT:MODEL [TOOL ...]`,
  for example `Assisted-by: OpenCode:Deepseek`. Do not use `Co-Authored-By` for AI.
  The mechanical `ALL: Sync with ScummVM rev: <sha>` commit is exempt.
- A human remains responsible for the change. Do not claim work you did not verify.
- Do not generate art or media assets: no logos, icons, splash screens, or other
  shipped artwork.
- No slop. Understand every line you submit, build it, test what you can, and say
  what you could not test. Keep comments and commit messages short and in the
  surrounding style.

Commit style is ScummVM/NovelVM style (`SUBSYSTEM: Short summary`), not
Conventional Commits.

If a request conflicts with AI-GUIDELINES.md, stop and say so.
