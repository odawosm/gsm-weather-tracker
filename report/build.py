"""Build PROJECT_REPORT.pdf from PROJECT_REPORT.md + references.txt.

Citations are written as [@key] or [@key1, @key2] in the Markdown. They are numbered in order of first
appearance (IEEE style) and the reference list is appended in that order. The build fails if the text
cites a key that is not in references.txt, or if a reference in references.txt is never cited, so the
reference list can only contain sources the report actually uses.

Run:  python build.py   (needs the markdown and weasyprint packages)
"""
import re
import sys
from pathlib import Path

import markdown
from weasyprint import HTML

HERE = Path(__file__).parent
CSS = """
@page { size: A4; margin: 22mm 20mm; @bottom-center { content: counter(page); font-size: 9pt; color: #777; } }
body { font-family: "Liberation Serif", "Times New Roman", serif; font-size: 11pt; line-height: 1.45; color: #111; }
h1 { font-family: "Liberation Sans", Arial, sans-serif; font-size: 19pt; text-align: center; margin: 0 0 4pt; }
h2 { font-family: "Liberation Sans", Arial, sans-serif; font-size: 13.5pt; margin: 20pt 0 8pt; break-after: avoid; }
h3 { font-family: "Liberation Sans", Arial, sans-serif; font-size: 11.5pt; margin: 14pt 0 6pt; break-after: avoid; }
p { margin: 0 0 8pt; text-align: justify; }
.titleblock { text-align: center; margin-bottom: 18pt; }
.titleblock p { text-align: center; margin: 2pt 0; }
.abstract { margin: 0 24pt 10pt; font-size: 10.5pt; }
table { border-collapse: collapse; width: 100%; margin: 8pt 0 10pt; font-size: 9.5pt; break-inside: auto; }
th, td { border: 1px solid #999; padding: 4pt 6pt; text-align: left; vertical-align: top; }
thead th { background: #eceff3; }
tr { break-inside: avoid; }
figure { margin: 10pt 0 14pt; text-align: center; break-inside: avoid; }
figure img { max-width: 100%; max-height: 190mm; }
figcaption { font-size: 9.5pt; margin-top: 4pt; }
blockquote { margin: 4pt 0 10pt 24pt; }
code { font-family: "DejaVu Sans Mono", monospace; font-size: 9pt; }
pre { background: #f5f6f8; border: 1px solid #ddd; padding: 7pt; font-size: 8.8pt; line-height: 1.3; white-space: pre-wrap; break-inside: avoid; }
ol.refs { padding-left: 0; list-style: none; font-size: 9.5pt; }
ol.refs li { margin: 0 0 5pt 28pt; text-indent: -28pt; text-align: left; }
ol.refs li span.n { display: inline-block; width: 28pt; text-indent: 0; }
li { margin: 2pt 0; }
"""


def load_refs():
    refs = {}
    for line in (HERE / "references.txt").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        key, text = line.split("|", 1)
        refs[key.strip()] = text.strip()
    return refs


def main():
    refs = load_refs()
    md = (HERE / "PROJECT_REPORT.md").read_text(encoding="utf-8")
    order = []  # keys in order of first citation

    def cite(m):
        nums = []
        for key in re.split(r"[,\s]+", m.group(1)):
            key = key.lstrip("@")
            if not key:
                continue
            if key not in refs:
                sys.exit(f"ERROR: citation [@{key}] is not in references.txt")
            if key not in order:
                order.append(key)
            nums.append(order.index(key) + 1)
        return ", ".join(f"[{n}]" for n in nums)

    body = re.sub(r"\[(@[\w\-]+(?:[,\s]+@[\w\-]+)*)\]", cite, md)
    unused = sorted(set(refs) - set(order))
    if unused:
        sys.exit(f"ERROR: references never cited (remove them or cite them): {', '.join(unused)}")
    if re.search(r"\[@", body):
        sys.exit("ERROR: a citation marker was left unresolved")

    ref_items = "".join(f'<li><span class="n">[{i}]</span>{markdown.markdown(refs[k])[3:-4]}</li>'
                        for i, k in enumerate(order, 1))
    html = markdown.markdown(body, extensions=["tables", "fenced_code", "sane_lists", "md_in_html", "attr_list"])
    html = html.replace("<!--REFERENCES-->", f'<ol class="refs">{ref_items}</ol>')
    page = f'<!doctype html><html><head><meta charset="utf-8"><style>{CSS}</style></head><body>{html}</body></html>'
    out = HERE / "PROJECT_REPORT.pdf"
    HTML(string=page, base_url=str(HERE)).write_pdf(out)
    print(f"{out}: {len(order)} references, {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
