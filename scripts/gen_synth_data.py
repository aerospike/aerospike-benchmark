#!/usr/bin/env python3
"""Generate the asbench synthetic data dictionaries (src/main/synth_data_*.c and
src/include/synth_data_tables.h) from the English word lists in gofakeit and
faker, pinned to fixed upstream commits."""

import argparse
import dataclasses
import difflib
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

GOFAKEIT_TAG = "v7.17.1"
GOFAKEIT_SHA = "0edecf3ab8e582cb6fb63216e64634ff524624a2"
FAKER_TAG = "v10.6.0"
FAKER_SHA = "2cb04231a6ace91a59ebe577c653f4ec66478ca3"

GOFAKEIT_RAW = "https://raw.githubusercontent.com/brianvoe/gofakeit/{sha}/{path}"
FAKER_RAW = "https://raw.githubusercontent.com/faker-js/faker/{sha}/{path}"
GOFAKEIT_DATA_DIR = "data"
FAKER_DATA_DIR = "src/locales/en"
GOFAKEIT_LICENSE = "LICENSE.txt"
FAKER_LICENSE = "LICENSE"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CACHE = os.path.join(REPO_ROOT, "target", "synth-cache")
HEADER_PATH = "src/include/synth_data_tables.h"
SOURCE_PATH = "src/main/synth_data_{group}.c"
FETCH_TIMEOUT = 30
MAX_WORD_LEN = 255
BANNED_CHARS = frozenset('"\\?{}')

GO_ENTRY = re.compile(r'"(?P<key>[a-z_]+)":\s*\{(?P<body>[^{}]*)\}', re.DOTALL)
GO_STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
CODE_RE = re.compile(r"[A-Z]{2}")


class SynthDataError(Exception):
	pass


@dataclasses.dataclass(frozen=True)
class Source:
	upstream: str
	path: str
	var: str = None
	keys: tuple = ()


def gofakeit(path, var, *keys):
	return Source("gofakeit", path, var, keys)


def faker(path, *keys):
	return Source("faker", path, None, keys)


def single_word(s):
	return " " not in s


def no_dot(s):
	return "." not in s


def has_dot(s):
	return "." in s


def country_code(s):
	return CODE_RE.fullmatch(s) is not None


@dataclasses.dataclass(frozen=True)
class Dict:
	name: str
	group: str
	sources: tuple = ()
	transforms: tuple = ()
	filters: tuple = ()
	min_count: int = 1
	union_of: tuple = ()


@dataclasses.dataclass(frozen=True)
class Pair:
	name: str
	partner: str
	group: str
	source: Source
	partner_source: Source
	partner_filter: object
	min_count: int
	exact_count: int = None
	spot_checks: tuple = ()


WORD_PARTS = ("NOUN", "ADJECTIVE", "VERB", "ADVERB")

CATALOG = (
	Dict("FIRST_NAME", "person", (
		gofakeit("person.go", "Person", "first"),
		faker("person/first_name.ts", "generic", "female", "male"),
	), min_count=2000),
	Dict("LAST_NAME", "person", (
		gofakeit("person.go", "Person", "last"),
		faker("person/last_name.ts", "generic"),
	), min_count=750),
	Dict("STREET_NAME", "address", (
		faker("location/street_name.ts"),
	), transforms=("strip_street_suffix",), min_count=300),
	Dict("STREET_SUFFIX", "address", (
		gofakeit("address.go", "Address", "street_name"),
		faker("location/street_suffix.ts"),
	), min_count=15),
	Dict("CITY", "address", (
		gofakeit("address.go", "Address", "city"),
		faker("location/city_name.ts"),
	), min_count=500),
	Pair("STATE", "STATE_ABBR", "address",
		faker("location/state.ts"),
		faker("location/state_abbr.ts"),
		country_code, 50, exact_count=50,
		spot_checks=(("Alabama", "AL"), ("Alaska", "AK"), ("California", "CA"), ("Wyoming", "WY"))),
	Pair("COUNTRY", "COUNTRY_CODE", "address",
		gofakeit("address.go", "Address", "country"),
		gofakeit("address.go", "Address", "country_abr"),
		country_code, 200,
		spot_checks=(("Andorra", "AD"), ("Germany", "DE"), ("Japan", "JP"),
			("United States of America", "US"), ("Zimbabwe", "ZW"))),
	Dict("COMPANY_NAME", "company", (
		gofakeit("company.go", "Company", "name"),
	), min_count=300),
	Dict("COMPANY_SUFFIX", "company", (
		gofakeit("company.go", "Company", "suffix"),
		faker("company/legal_entity_type.ts"),
	), min_count=3),
	Dict("BUZZWORD", "company", (
		gofakeit("company.go", "Company", "buzzwords"),
	), transforms=("lower",), min_count=100),
	Dict("JOB_TITLE", "person", (
		gofakeit("job.go", "Job", "title"),
	), min_count=100),
	Dict("PRODUCT_NAME", "product", (
		gofakeit("product.go", "Product", "name"),
	), min_count=300),
	Dict("PRODUCT_ADJECTIVE", "product", (
		gofakeit("product.go", "Product", "adjective"),
	), min_count=100),
	Dict("PRODUCT_MATERIAL", "product", (
		gofakeit("product.go", "Product", "material"),
	), min_count=40),
	Dict("PRODUCT_CATEGORY", "product", (
		gofakeit("product.go", "Product", "category"),
	), min_count=40),
	Dict("COLOR", "product", (
		gofakeit("colors.go", "Colors", "safe"),
		faker("color/human.ts"),
	), transforms=("lower",), min_count=12),
	Dict("NOUN", "words", (
		gofakeit("word.go", "Word", "noun_common", "noun_concrete", "noun_abstract",
			"noun_countable", "noun_uncountable"),
	), transforms=("lower",), filters=(single_word,), min_count=200),
	Dict("ADJECTIVE", "words", (
		gofakeit("word.go", "Word", "adjective_descriptive"),
	), transforms=("lower",), filters=(single_word,), min_count=100),
	Dict("VERB", "words", (
		gofakeit("word.go", "Word", "verb_action", "verb_transitive"),
	), transforms=("lower",), filters=(single_word,), min_count=100),
	Dict("ADVERB", "words", (
		gofakeit("word.go", "Word", "adverb_manner"),
	), transforms=("lower",), filters=(single_word,), min_count=80),
	Dict("WORD", "words", union_of=WORD_PARTS, transforms=("lower",),
		filters=(single_word,), min_count=400),
	Dict("LOREM", "words", (
		gofakeit("lorem.go", "Lorem", "word"),
		faker("lorem/word.ts"),
	), transforms=("lower",), filters=(single_word,), min_count=150),
	Dict("TLD", "internet", (
		gofakeit("internet.go", "Internet", "domain_suffix"),
		faker("internet/domain_suffix.ts"),
	), transforms=("lower",), filters=(single_word, no_dot), min_count=5),
	Dict("EMAIL_PROVIDER", "internet", (
		faker("internet/free_email.ts"),
		faker("internet/example_email.ts"),
	), transforms=("lower",), filters=(single_word, has_dot), min_count=3),
)

GROUPS = ("person", "address", "company", "product", "words", "internet")


def catalog_names(catalog=CATALOG):
	names = []
	for entry in catalog:
		names.append(entry.name)
		if isinstance(entry, Pair):
			names.append(entry.partner)
	return names


def unescape(s):
	return re.sub(r"\\(['\"\\])", r"\1", s)


def parse_go(text, var, keys):
	decl = re.search(r"\bvar\s+" + re.escape(var) + r"\s*=\s*map\[string\]\[\]string\s*\{", text)
	if decl is None:
		raise SynthDataError(f"go: var {var} not found")
	found = {}
	for m in GO_ENTRY.finditer(text, decl.end()):
		found.setdefault(m.group("key"), [unescape(s) for s in GO_STRING.findall(m.group("body"))])
	missing = [k for k in keys if k not in found]
	if missing:
		raise SynthDataError(f"go: var {var} missing keys {', '.join(missing)}")
	return {k: found[k] for k in keys}


def ts_tokens(text):
	i = 0
	n = len(text)
	while i < n:
		c = text[i]
		if c.isspace():
			i += 1
		elif text.startswith("//", i):
			j = text.find("\n", i)
			i = n if j < 0 else j
		elif text.startswith("/*", i):
			j = text.find("*/", i + 2)
			if j < 0:
				raise SynthDataError("ts: unterminated block comment")
			i = j + 2
		elif c in "'\"":
			j = i + 1
			buf = []
			while True:
				if j >= n or text[j] == "\n":
					raise SynthDataError("ts: unterminated string literal")
				ch = text[j]
				if ch == c:
					break
				if ch == "\\" and j + 1 < n:
					nxt = text[j + 1]
					buf.append(nxt if nxt in "'\"\\" else ch + nxt)
					j += 2
				else:
					buf.append(ch)
					j += 1
			yield ("str", "".join(buf))
			i = j + 1
		elif c.isalnum() or c in "_$":
			j = i
			while j < n and (text[j].isalnum() or text[j] in "_$"):
				j += 1
			yield ("id", text[i:j])
			i = j
		else:
			yield ("punct", c)
			i += 1


def _ts_expect(toks, pos, kind, value=None):
	if pos >= len(toks) or toks[pos][0] != kind or (value is not None and toks[pos][1] != value):
		got = toks[pos] if pos < len(toks) else ("eof", "")
		raise SynthDataError(f"ts: expected {value or kind}, got {got[1]!r}")
	return pos + 1


def _ts_array(toks, pos):
	pos = _ts_expect(toks, pos, "punct", "[")
	values = []
	while True:
		if pos < len(toks) and toks[pos] == ("punct", "]"):
			return values, pos + 1
		pos = _ts_expect(toks, pos, "str")
		values.append(toks[pos - 1][1])
		if pos < len(toks) and toks[pos] == ("punct", ","):
			pos += 1
		elif pos >= len(toks) or toks[pos] != ("punct", "]"):
			raise SynthDataError("ts: expected , or ] in array")


def _ts_object(toks, pos):
	pos = _ts_expect(toks, pos, "punct", "{")
	obj = {}
	while True:
		if pos < len(toks) and toks[pos] == ("punct", "}"):
			return obj, pos + 1
		if pos >= len(toks) or toks[pos][0] not in ("id", "str"):
			raise SynthDataError("ts: expected object key")
		key = toks[pos][1]
		pos = _ts_expect(toks, pos + 1, "punct", ":")
		obj[key], pos = _ts_array(toks, pos)
		if pos < len(toks) and toks[pos] == ("punct", ","):
			pos += 1
		elif pos >= len(toks) or toks[pos] != ("punct", "}"):
			raise SynthDataError("ts: expected , or } in object")


def parse_ts(text):
	toks = list(ts_tokens(text))
	for i in range(len(toks) - 1):
		if toks[i] == ("id", "export") and toks[i + 1] == ("id", "default"):
			pos = i + 2
			break
	else:
		raise SynthDataError("ts: export default not found")
	if pos < len(toks) and toks[pos] == ("punct", "["):
		return _ts_array(toks, pos)[0]
	if pos < len(toks) and toks[pos] == ("punct", "{"):
		return _ts_object(toks, pos)[0]
	raise SynthDataError("ts: export default is neither an array nor an object")


def ts_values(text, keys=()):
	parsed = parse_ts(text)
	if isinstance(parsed, list):
		return parsed
	if not keys:
		raise SynthDataError("ts: object export needs keys")
	missing = [k for k in keys if k not in parsed]
	if missing:
		raise SynthDataError(f"ts: object missing keys {', '.join(missing)}")
	return [v for k in keys for v in parsed[k]]


def clean(s):
	s = s.strip()
	if not s:
		return None
	if any(not 0x20 <= ord(c) <= 0x7E or c in BANNED_CHARS for c in s):
		return None
	return re.sub(" {2,}", " ", s)


def sort_key(s):
	return (s.casefold(), s)


def dedupe_sorted(values):
	out = []
	seen = set()
	for s in sorted(values, key=sort_key):
		k = s.casefold()
		if k not in seen:
			seen.add(k)
			out.append(s)
	return out


def strip_street_suffix(s, suffixes):
	tokens = s.split(" ")
	if tokens[-1].casefold() in suffixes:
		tokens = tokens[:-1]
	return " ".join(tokens) or None


def normalize(values, transforms=(), filters=(), suffixes=frozenset()):
	out = []
	for v in values:
		s = clean(v)
		for t in transforms:
			if s is None:
				break
			if t == "lower":
				s = s.lower()
			elif t == "strip_street_suffix":
				s = strip_street_suffix(s, suffixes)
			else:
				raise SynthDataError(f"unknown transform {t}")
		if s is not None and all(f(s) for f in filters):
			out.append(s)
	return dedupe_sorted(out)


def build_pair(names, codes, code_filter=country_code, label="pair"):
	if len(names) != len(codes):
		raise SynthDataError(f"{label}: source lengths differ ({len(names)} != {len(codes)})")
	pairs = []
	for a, b in zip(names, codes):
		a = clean(a)
		b = clean(b)
		if a is None or b is None or not code_filter(b):
			continue
		pairs.append((a, b))
	pairs.sort(key=lambda p: sort_key(p[0]))
	out_names = []
	out_codes = []
	seen = set()
	for a, b in pairs:
		if a.casefold() not in seen:
			seen.add(a.casefold())
			out_names.append(a)
			out_codes.append(b)
	return out_names, out_codes


def validate(name, words, min_count=1):
	if not words:
		raise SynthDataError(f"{name}: empty dictionary")
	for w in words:
		if len(w) < 1:
			raise SynthDataError(f"{name}: empty word")
		if len(w) > MAX_WORD_LEN:
			raise SynthDataError(f"{name}: word longer than {MAX_WORD_LEN}: {w[:40]!r}")
		if clean(w) != w:
			raise SynthDataError(f"{name}: unclean word {w!r}")
	if len(words) < min_count:
		raise SynthDataError(f"{name}: {len(words)} entries, need at least {min_count}")


class Upstream:
	def __init__(self, name, raw, data_dir, license_path, sha, src=None, cache=DEFAULT_CACHE):
		self.name = name
		self.raw = raw
		self.data_dir = data_dir
		self.license_path = license_path
		self.sha = sha
		self.src = src
		self.cache = cache
		self._texts = {}

	def warn_if_src_mismatch(self):
		if self.src is None or not os.path.exists(os.path.join(self.src, ".git")):
			return
		try:
			head = subprocess.run(["git", "-C", self.src, "rev-parse", "HEAD"],
				capture_output=True, text=True, check=True).stdout.strip()
		except (OSError, subprocess.CalledProcessError):
			return
		if head != self.sha:
			print(f"warning: {self.name} checkout {self.src} is at {head}, recording {self.sha}",
				file=sys.stderr)

	def read(self, path):
		if path in self._texts:
			return self._texts[path]
		if self.src is not None:
			local = os.path.join(self.src, path)
			if not os.path.exists(local):
				raise SynthDataError(f"{self.name}: {local} not found")
		else:
			local = os.path.join(self.cache, self.sha, path)
			if not os.path.exists(local):
				self._fetch(path, local)
		with open(local, encoding="utf-8") as f:
			text = f.read()
		self._texts[path] = text
		return text

	def _fetch(self, path, local):
		url = self.raw.format(sha=self.sha, path=path)
		try:
			with urllib.request.urlopen(url, timeout=FETCH_TIMEOUT) as resp:
				body = resp.read()
		except (urllib.error.URLError, OSError) as e:
			raise SynthDataError(f"{self.name}: fetch {url} failed: {e}") from e
		os.makedirs(os.path.dirname(local), exist_ok=True)
		tmp = local + ".tmp"
		with open(tmp, "wb") as f:
			f.write(body)
		os.replace(tmp, local)

	def data(self, path):
		return self.read(self.data_dir + "/" + path)

	def license(self):
		return self.read(self.license_path)


def load_source(upstreams, src):
	text = upstreams[src.upstream].data(src.path)
	if src.upstream == "gofakeit":
		parsed = parse_go(text, src.var, src.keys)
		return [v for k in src.keys for v in parsed[k]]
	return ts_values(text, src.keys)


def build_all(upstreams, catalog=CATALOG):
	entries = {e.name: e for e in catalog}
	results = {}

	def build(name):
		if name in results:
			return results[name]
		entry = entries[name]
		if isinstance(entry, Pair):
			names, codes = build_pair(load_source(upstreams, entry.source),
				load_source(upstreams, entry.partner_source), entry.partner_filter, entry.name)
			for a, b in entry.spot_checks:
				if a not in names or codes[names.index(a)] != b:
					raise SynthDataError(f"{entry.name}: spot check {a}/{b} failed")
			if entry.exact_count is not None and len(names) != entry.exact_count:
				raise SynthDataError(f"{entry.name}: {len(names)} entries, expected {entry.exact_count}")
			validate(entry.name, names, entry.min_count)
			validate(entry.partner, codes, entry.min_count)
			results[entry.name] = names
			results[entry.partner] = codes
			return names
		suffixes = frozenset()
		if "strip_street_suffix" in entry.transforms:
			suffixes = frozenset(s.casefold() for s in build("STREET_SUFFIX"))
		values = [v for part in entry.union_of for v in build(part)]
		for src in entry.sources:
			values.extend(load_source(upstreams, src))
		words = normalize(values, entry.transforms, entry.filters, suffixes)
		validate(entry.name, words, entry.min_count)
		results[name] = words
		return words

	for entry in catalog:
		build(entry.name)
	return results


def generated_block(gofakeit_sha, faker_sha):
	return (
		"/*\n"
		" * Generated by scripts/gen_synth_data.py. Do not edit.\n"
		f" * Sources: gofakeit {gofakeit_sha} (MIT), faker {faker_sha} (MIT). See LICENSE.md.\n"
		" */\n"
	)


def offsets(words):
	off = [0]
	for w in words:
		off.append(off[-1] + len(w) + 1)
	return off


def emit_dict(name, words):
	stem = name.lower()
	off = offsets(words)
	lines = [f"static const char {stem}_pool[] ="]
	lines.extend(f'\t"{w}\\0"' for w in words)
	lines.append("\t;")
	lines.append("")
	lines.append(f"static const uint32_t {stem}_off[] = {{")
	for i in range(0, len(off), 16):
		lines.append("\t" + ", ".join(str(o) for o in off[i:i + 16]) + ",")
	lines.append("};")
	lines.append("")
	max_len = max(len(w) for w in words)
	lines.append(f"const synth_dict_t SYNTH_DICT_{name} = "
		f"{{ {stem}_pool, {stem}_off, {len(words)}, {max_len} }};")
	return "\n".join(lines) + "\n"


def emit_source(names, results, gofakeit_sha, faker_sha):
	parts = [generated_block(gofakeit_sha, faker_sha), "\n#include <synth_data.h>\n"]
	for name in names:
		parts.append("\n")
		parts.append(emit_dict(name, results[name]))
	return "".join(parts)


def emit_header(names, gofakeit_sha, faker_sha):
	lines = [
		"#pragma once",
		"",
		f'#define SYNTH_DATA_GOFAKEIT_SHA "{gofakeit_sha}"',
		f'#define SYNTH_DATA_FAKER_SHA "{faker_sha}"',
		"",
	]
	lines.extend(f"extern const synth_dict_t SYNTH_DICT_{n};" for n in names)
	lines.append("")
	lines.append("#define SYNTH_DICT_LIST(X) \\")
	lines.extend(f"\tX({n}) \\" for n in names[:-1])
	lines.append(f"\tX({names[-1]})")
	return generated_block(gofakeit_sha, faker_sha) + "\n" + "\n".join(lines) + "\n"


def render(results, gofakeit_sha, faker_sha, catalog=CATALOG):
	names = catalog_names(catalog)
	group_of = {}
	for entry in catalog:
		group_of[entry.name] = entry.group
		if isinstance(entry, Pair):
			group_of[entry.partner] = entry.group
	outputs = {HEADER_PATH: emit_header(names, gofakeit_sha, faker_sha)}
	for group in GROUPS:
		members = [n for n in names if group_of[n] == group]
		outputs[SOURCE_PATH.format(group=group)] = emit_source(members, results, gofakeit_sha, faker_sha)
	return outputs


def stats(results, catalog=CATALOG):
	rows = []
	for name in catalog_names(catalog):
		words = results[name]
		rows.append((name, len(words), max(len(w) for w in words), offsets(words)[-1]))
	return rows


def check(outputs, root):
	drift = 0
	for rel, text in outputs.items():
		path = os.path.join(root, rel)
		try:
			with open(path, encoding="utf-8", newline="") as f:
				current = f.read()
		except FileNotFoundError:
			print(f"drift: {rel} missing")
			drift += 1
			continue
		if current == text:
			continue
		diff = list(difflib.unified_diff(current.splitlines(), text.splitlines(),
			f"a/{rel}", f"b/{rel}", lineterm="", n=1))
		added = sum(1 for d in diff if d.startswith("+") and not d.startswith("+++"))
		removed = sum(1 for d in diff if d.startswith("-") and not d.startswith("---"))
		print(f"drift: {rel} (+{added} -{removed} lines)")
		for d in diff[:20]:
			print(f"    {d}")
		if len(diff) > 20:
			print(f"    ... {len(diff) - 20} more diff lines")
		drift += 1
	if drift:
		print(f"synth-data: {drift} of {len(outputs)} generated files are out of date; "
			"run scripts/gen_synth_data.py")
		return 1
	print(f"synth-data: {len(outputs)} generated files up to date")
	return 0


def write(outputs, root):
	for rel, text in outputs.items():
		path = os.path.join(root, rel)
		try:
			with open(path, encoding="utf-8", newline="") as f:
				if f.read() == text:
					print(f"unchanged {rel}")
					continue
		except FileNotFoundError:
			pass
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "w", encoding="utf-8", newline="") as f:
			f.write(text)
		print(f"wrote {rel}")


def parse_args(argv):
	p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	g = p.add_mutually_exclusive_group()
	g.add_argument("--gofakeit-src", metavar="DIR", help="read gofakeit from a local checkout")
	g.add_argument("--gofakeit-sha", metavar="SHA", default=GOFAKEIT_SHA,
		help=f"gofakeit commit to fetch (default {GOFAKEIT_SHA}, {GOFAKEIT_TAG})")
	f = p.add_mutually_exclusive_group()
	f.add_argument("--faker-src", metavar="DIR", help="read faker from a local checkout")
	f.add_argument("--faker-sha", metavar="SHA", default=FAKER_SHA,
		help=f"faker commit to fetch (default {FAKER_SHA}, {FAKER_TAG})")
	p.add_argument("--out", metavar="ROOT", default=REPO_ROOT,
		help="repository root to write generated files under (default: this repo)")
	p.add_argument("--cache", metavar="DIR", default=DEFAULT_CACHE,
		help="directory for fetched upstream files (default: target/synth-cache)")
	p.add_argument("--check", action="store_true",
		help="regenerate in memory and exit 1 if the checked-in files differ")
	p.add_argument("--print-licenses", action="store_true",
		help="print the upstream LICENSE files at the pinned commits and exit")
	return p.parse_args(argv)


def main(argv=None):
	args = parse_args(argv)
	upstreams = {
		"gofakeit": Upstream("gofakeit", GOFAKEIT_RAW, GOFAKEIT_DATA_DIR, GOFAKEIT_LICENSE,
			args.gofakeit_sha, args.gofakeit_src, args.cache),
		"faker": Upstream("faker", FAKER_RAW, FAKER_DATA_DIR, FAKER_LICENSE,
			args.faker_sha, args.faker_src, args.cache),
	}
	try:
		for u in upstreams.values():
			u.warn_if_src_mismatch()
		if args.print_licenses:
			for u in upstreams.values():
				print(f"==> {u.name} {u.sha} {u.license_path}")
				print(u.license().rstrip("\n"))
				print()
			return 0
		results = build_all(upstreams)
		outputs = render(results, upstreams["gofakeit"].sha, upstreams["faker"].sha)
	except SynthDataError as e:
		print(f"error: {e}", file=sys.stderr)
		return 2
	if args.check:
		return check(outputs, args.out)
	rows = stats(results)
	for name, n, max_len, size in rows:
		print(f"{name:<18} n={n:<6} max_len={max_len:<4} pool={size}")
	print(f"{'total':<18} n={sum(r[1] for r in rows):<6} {'':13}pool={sum(r[3] for r in rows)}")
	write(outputs, args.out)
	return 0


if __name__ == "__main__":
	sys.exit(main())
