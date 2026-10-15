#!/usr/bin/env python3
"""Builds docs/user-guide/Purrlude User Guide.pdf from the guide's Markdown pages.

The pages are joined in the order README.md lists them, each starting on a new page, after a
cover and a table of contents with page numbers. Links between pages become links within the
PDF, and the headings become its bookmarks.

Needs Python's `markdown` package and the `weasyprint` command (e.g. `brew install weasyprint`).

    python3 tools/docs/build_user_guide_pdf.py [output.pdf]
"""

import datetime
import html
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import markdown

GUIDE = Path(__file__).resolve().parents[2] / "docs" / "user-guide"
DEFAULT_OUTPUT = GUIDE / "Purrlude User Guide.pdf"

CSS = """
@page {
    size: A4;
    margin: 20mm 18mm 20mm 18mm;
    @top-left { content: string(section); font-size: 8.5pt; color: #6b7280; }
    @bottom-right { content: counter(page); font-size: 8.5pt; color: #6b7280; }
}
@page :first { @top-left { content: none; } @bottom-right { content: none; } }
@page front { @top-left { content: none; } }

html { font-family: "Helvetica Neue", Helvetica, Arial, sans-serif; font-size: 10pt; line-height: 1.45; color: #1f2933; }
h1, h2, h3 { color: #12324a; line-height: 1.2; break-after: avoid; }
h1 { font-size: 22pt; margin: 0 0 10pt; string-set: section content(); bookmark-level: 1; }
h2 { font-size: 14pt; margin: 18pt 0 6pt; bookmark-level: 2; }
h3 { font-size: 11.5pt; margin: 14pt 0 4pt; bookmark-level: 3; }
p, ul, ol { margin: 0 0 7pt; }
li { margin-bottom: 2pt; }
a { color: #1d5f99; text-decoration: none; }
code { font-family: Menlo, Consolas, monospace; font-size: 8.8pt; background: #eef2f6; padding: 0 2pt; border-radius: 2pt; }
pre { background: #eef2f6; padding: 6pt 8pt; border-radius: 3pt; white-space: pre-wrap; }
pre code { padding: 0; }
img { max-width: 100%; height: auto; border: 0.5pt solid #c9d2db; border-radius: 3pt; }
img[align=right] { float: right; margin: 2pt 0 8pt 12pt; max-width: 45%; }
p:has(> img:only-child) { break-inside: avoid; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 10pt; font-size: 9pt; break-inside: auto; }
th, td { border: 0.5pt solid #c9d2db; padding: 3pt 5pt; text-align: left; vertical-align: top; }
th { background: #eef2f6; }
tr { break-inside: avoid; }
br[clear] { clear: both; }
hr { display: none; }

section.chapter { break-before: page; }
section.chapter > h1 { padding-bottom: 6pt; border-bottom: 1.5pt solid #12324a; }

.cover { page: front; height: 240mm; display: flex; flex-direction: column; justify-content: center; align-items: center; text-align: center; }
.cover img.logo { width: 75%; border: 0; border-radius: 6pt; margin-bottom: 10pt; }
.cover h1 { font-size: 28pt; border: 0; string-set: none; bookmark-level: none; margin-bottom: 6pt; }
.cover .subtitle { font-size: 14pt; color: #4b5563; margin-bottom: 24pt; }
.cover img { margin: 10pt 0 18pt; }
.cover .date { color: #6b7280; }

.toc { page: front; break-before: page; }
.toc h1 { string-set: none; bookmark-level: 1; }
.toc ol { list-style: none; padding: 0; margin: 0; }
.toc > ol > li { font-weight: bold; margin-top: 7pt; }
.toc li ol { font-weight: normal; padding-left: 14pt; font-size: 9.5pt; }
.toc li ol li { margin: 1pt 0; }
.toc a { color: #1f2933; }
.toc a::after { content: leader(".") target-counter(attr(href), page); color: #6b7280; }

.about { page: front; break-before: page; }
.about h1 { string-set: none; }
"""


def slugify(value, separator):
    value = re.sub(r"[^\w\s-]", "", value.lower()).strip()
    return re.sub(r"[\s]+", separator, value)


def github_lists(text):
    """GitHub's Markdown lets a list follow a line directly and nest by two spaces; Python's
    Markdown needs a blank line before it and four spaces.
    """
    lines, out = text.splitlines(), []
    list_item = re.compile(r"^(\s*)([-*]|\d+\.)\s")

    for i, line in enumerate(lines):
        indent = len(line) - len(line.lstrip(" "))

        if indent and line.strip():
            line = " " * (indent * 2) + line.lstrip(" ")

        previous = lines[i - 1] if i > 0 else ""

        if list_item.match(line) and previous.strip() and not list_item.match(previous):
            out.append("")

        out.append(line)

    return "\n".join(out)


def sized_images(page_html):
    """width="N" attributes as CSS, which WeasyPrint follows."""
    return re.sub(r'(<img [^>]*?)width="(\d+)(%?)"', lambda m: f'{m.group(1)}style="width: {m.group(2)}{m.group(3) or "px"}"', page_html)


def to_html(text):
    return markdown.markdown(github_lists(text), extensions=["tables", "toc", "fenced_code"],
                             extension_configs={"toc": {"slugify": slugify}})


def chapter_order(readme):
    return re.findall(r"\|\s*\d+\s*\|\s*\[[^\]]+\]\((\d\d-[^)]+\.md)\)", readme)


def strip_nav(text):
    """Drops a page's line of links to the previous and next pages."""
    return "\n".join(line for line in text.splitlines()
                     if not re.match(r"^\[(←|Contents\])", line.strip()))


def prefix_ids(page_html, stem):
    """Makes heading ids unique across pages, and points links at them."""
    page_html = re.sub(r'id="([^"]+)"', lambda m: f'id="{stem}--{m.group(1)}"', page_html)

    def link(m):
        target, anchor = m.group(1), m.group(2)
        if target == "README.md":
            return 'href="#contents"'
        target_stem = Path(target).stem
        return f'href="#{target_stem}--{anchor}"' if anchor else f'href="#{target_stem}"'

    return re.sub(r'href="([\w.-]+\.md)(?:#([\w-]+))?"', link, page_html)


def build(output):
    readme = (GUIDE / "README.md").read_text(encoding="utf-8")
    chapters, toc_items = [], []

    for name in chapter_order(readme):
        stem = Path(name).stem
        page_html = sized_images(prefix_ids(to_html(strip_nav((GUIDE / name).read_text(encoding="utf-8"))), stem))

        # The page's title is the chapter's anchor.
        page_html = re.sub(r'<h1 id="[^"]+">', f'<h1 id="{stem}">', page_html, count=1)
        chapters.append(f'<section class="chapter">{page_html}</section>')

        title = re.search(r'<h1 id="[^"]+">(.*?)</h1>', page_html).group(1)
        subsections = "".join(f'<li><a href="#{anchor}">{text}</a></li>'
                              for anchor, text in re.findall(r'<h2 id="([^"]+)">(.*?)</h2>', page_html))
        toc_items.append(f'<li><a href="#{stem}">{title}</a>' + (f"<ol>{subsections}</ol>" if subsections else "") + "</li>")

    # The index's introduction and conventions, without its table (that's the contents) or
    # the note for developers at the end.
    intro = readme.split("## Contents")[0]
    intro = re.sub(r"^# .*\n", "", intro, count=1)
    intro = re.sub(r'<img [^>]*>\n?', "", intro)
    intro = "\n".join(line for line in intro.splitlines() if "build_user_guide_pdf" not in line)   # how to make this PDF
    conventions = readme.split("## Conventions")[1].split("---")[0]
    about = to_html("# About This Guide\n" + intro + "\n## Conventions\n" + conventions)
    about = about.replace('<h1 id="about-this-guide">', '<h1 id="about">')

    today = datetime.date.today().strftime("%B %Y")
    document = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Purrlude User Guide</title><style>{CSS}</style></head>
<body>
<div class="cover">
  <img class="logo" src="images/Purrlude-Cat-Logo.png" alt="Purrlude">
  <h1>User Guide</h1>
  <div class="date">{html.escape(today)}</div>
</div>
<div class="toc" id="contents"><h1>Contents</h1><ol><li><a href="#about">About This Guide</a></li>{"".join(toc_items)}</ol></div>
<div class="about">{about}</div>
{"".join(chapters)}
</body></html>"""

    with tempfile.NamedTemporaryFile("w", suffix=".html", dir=GUIDE, delete=False, encoding="utf-8") as f:
        f.write(document)
        page = Path(f.name)

    try:
        subprocess.run(["weasyprint", str(page), str(output)], check=True)
    finally:
        page.unlink()

    print(f"Wrote {output}")


if __name__ == "__main__":
    build(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT)
