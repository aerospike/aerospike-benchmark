"""Tests for scripts/gen_synth_data.py."""

import contextlib
import importlib.util
import io
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_synth_data.py")
_spec = importlib.util.spec_from_file_location("gen_synth_data", SCRIPT)
gen = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = gen
_spec.loader.exec_module(gen)

GO_FIXTURE = """package data

var Other = map[string][]string{
	"name": {"wrong"},
}

// Address consists of address information
var Address = map[string][]string{
	"street_name": {"Alley", "Avenue"},
	"city":        {"New York City",
		"Say \\"hi\\"", "St. Louis"},
	"empty":       {},
}
"""

TS_FLAT_FIXTURE = """/*
 * header comment with 'quotes' and [brackets]
 */
export default [
  'Alpha', // trailing 'comment'
  "Coeur d'Alene",
  'It\\'s',
  "Say \\"hi\\"",
  'back\\\\slash',
  'http://example.com',
];
"""

TS_OBJECT_FIXTURE = """export default {
  generic: ['A', "B"],
  // female: ['commented out'],
  female: [
    'C', /* inline */ 'D',
  ],
  male: ["E"],
};
"""


def have_network():
	try:
		with socket.create_connection(("raw.githubusercontent.com", 443), timeout=3):
			return True
	except OSError:
		return False


class GoParserTest(unittest.TestCase):
	def test_keys(self):
		parsed = gen.parse_go(GO_FIXTURE, "Address", ("street_name", "city", "empty"))
		self.assertEqual(parsed["street_name"], ["Alley", "Avenue"])
		self.assertEqual(parsed["city"], ["New York City", 'Say "hi"', "St. Louis"])
		self.assertEqual(parsed["empty"], [])

	def test_scoped_to_var(self):
		with self.assertRaises(gen.SynthDataError):
			gen.parse_go(GO_FIXTURE, "Address", ("name",))
		self.assertEqual(gen.parse_go(GO_FIXTURE, "Other", ("name",))["name"], ["wrong"])

	def test_missing(self):
		with self.assertRaises(gen.SynthDataError):
			gen.parse_go(GO_FIXTURE, "Address", ("street_name", "zip"))
		with self.assertRaises(gen.SynthDataError):
			gen.parse_go(GO_FIXTURE, "Person", ("first",))


class TsParserTest(unittest.TestCase):
	def test_flat(self):
		self.assertEqual(gen.parse_ts(TS_FLAT_FIXTURE), [
			"Alpha", "Coeur d'Alene", "It's", 'Say "hi"', "back\\slash", "http://example.com",
		])

	def test_object(self):
		self.assertEqual(gen.parse_ts(TS_OBJECT_FIXTURE), {
			"generic": ["A", "B"], "female": ["C", "D"], "male": ["E"],
		})
		self.assertEqual(gen.ts_values(TS_OBJECT_FIXTURE, ("generic", "male")), ["A", "B", "E"])

	def test_flat_ignores_keys(self):
		self.assertEqual(gen.ts_values(TS_FLAT_FIXTURE, ("generic",))[0], "Alpha")

	def test_object_errors(self):
		with self.assertRaises(gen.SynthDataError):
			gen.ts_values(TS_OBJECT_FIXTURE)
		with self.assertRaises(gen.SynthDataError):
			gen.ts_values(TS_OBJECT_FIXTURE, ("generic", "neutral"))

	def test_malformed(self):
		for text in ("const x = ['a'];", "export default ['a' 'b'];", "export default ['a",
				"export default 42;", "/* open"):
			with self.subTest(text=text), self.assertRaises(gen.SynthDataError):
				gen.parse_ts(text)


class NormalizeTest(unittest.TestCase):
	def test_clean_keeps(self):
		for s in ("O'Hara", "Smith-Jones", "St. Louis", "Bonaire, Sint Eustatius", "AT&T", "3M"):
			self.assertEqual(gen.clean(s), s)
		self.assertEqual(gen.clean("  Foo   Bar  "), "Foo Bar")

	def test_clean_drops(self):
		for s in ("", "   ", "Café", "Åland", 'a"b', "a\\b", "why?", "{{x}}", "tab\there",
				"x\x7f"):
			with self.subTest(s=s):
				self.assertIsNone(gen.clean(s))

	def test_dedupe_sort(self):
		self.assertEqual(gen.dedupe_sorted(["beta", "Alpha", "alpha", "Beta", "gamma", "ALPHA"]),
			["ALPHA", "Beta", "gamma"])
		self.assertEqual(gen.dedupe_sorted(["b", "A", "a b", "a", "Ab"]), ["A", "a b", "Ab", "b"])

	def test_normalize_transforms(self):
		self.assertEqual(gen.normalize(["Foo", "foo bar", "FOO", "Bar?"], ("lower",), (gen.single_word,)),
			["foo"])
		self.assertEqual(gen.normalize(["com", "co.uk", "ORG"], ("lower",), (gen.no_dot,)), ["com", "org"])

	def test_strip_street_suffix(self):
		suffixes = frozenset({"street", "avenue", "road"})
		self.assertEqual(gen.strip_street_suffix("10th Street", suffixes), "10th")
		self.assertEqual(gen.strip_street_suffix("Yew Tree Close", suffixes), "Yew Tree Close")
		self.assertIsNone(gen.strip_street_suffix("Street", suffixes))
		self.assertEqual(gen.normalize(
			["10th Street", "Oak Avenue", "Yew Tree Close", "Street", "Main   Road", "Oak road"],
			("strip_street_suffix",), suffixes=suffixes), ["10th", "Main", "Oak", "Yew Tree Close"])


class PairTest(unittest.TestCase):
	def test_length_mismatch(self):
		with self.assertRaises(gen.SynthDataError):
			gen.build_pair(["Alpha", "Beta"], ["AA"])

	def test_filter_dedupe_sort(self):
		names, codes = gen.build_pair(
			["Zed", "Åland", "alpha", "Alpha", "Beta", "Gamma?"],
			["ZZ", "AX", "AB", "AA", "b1", "GG"])
		self.assertEqual(names, ["Alpha", "Zed"])
		self.assertEqual(codes, ["AA", "ZZ"])


class ValidateTest(unittest.TestCase):
	def test_failures(self):
		for words, min_count in (([], 1), (["x" * 256], 1), (["a", "b"], 3), (["a "], 1)):
			with self.subTest(words=words[:1], min_count=min_count), self.assertRaises(gen.SynthDataError):
				gen.validate("T", words, min_count)
		gen.validate("T", ["a", "x" * 255], 2)


class EmitTest(unittest.TestCase):
	WORDS = ["Alpha", "10th", "b'c", "x y", "007"]
	GSHA = "a" * 40
	FSHA = "b" * 40

	def test_offsets_layout(self):
		words = [f"w{i}" for i in range(20)]
		text = gen.emit_dict("MANY", words)
		lines = text.splitlines()
		start = lines.index("static const uint32_t many_off[] = {")
		self.assertEqual(lines[start + 1], "\t" + ", ".join(str(o) for o in gen.offsets(words)[:16]) + ",")
		self.assertEqual(lines[start + 2], "\t" + ", ".join(str(o) for o in gen.offsets(words)[16:]) + ",")
		self.assertEqual(lines[start + 3], "};")
		self.assertEqual(gen.offsets(words)[-1], sum(len(w) + 1 for w in words))
		self.assertTrue(text.endswith("const synth_dict_t SYNTH_DICT_MANY = { many_pool, many_off, 20, 3 };\n"))

	def test_header(self):
		text = gen.emit_header(["ONE", "TWO"], self.GSHA, self.FSHA)
		self.assertIn("#pragma once\n", text)
		self.assertNotIn("#include", text)
		self.assertIn(f'#define SYNTH_DATA_GOFAKEIT_SHA "{self.GSHA}"\n', text)
		self.assertIn("extern const synth_dict_t SYNTH_DICT_TWO;\n", text)
		self.assertTrue(text.endswith("#define SYNTH_DICT_LIST(X) \\\n\tX(ONE) \\\n\tX(TWO)\n"))

	@unittest.skipIf(shutil.which("cc") is None, "cc not available")
	def test_compiles_and_reads_back(self):
		include = os.path.join(gen.REPO_ROOT, "src", "include")
		with tempfile.TemporaryDirectory() as tmp:
			with open(os.path.join(tmp, "synth_data_tables.h"), "w") as f:
				f.write(gen.emit_header(["TINY"], self.GSHA, self.FSHA))
			with open(os.path.join(tmp, "tiny.c"), "w") as f:
				f.write(gen.emit_source(["TINY"], {"TINY": self.WORDS}, self.GSHA, self.FSHA))
			with open(os.path.join(tmp, "main.c"), "w") as f:
				f.write(
					"#include <stdio.h>\n"
					"#include <string.h>\n"
					"#include <synth_data.h>\n"
					"#define X(n) + 1\n"
					"int\n"
					"main(void)\n"
					"{\n"
					"\tconst synth_dict_t* d = &SYNTH_DICT_TINY;\n"
					"\tprintf(\"%d %s %u %u %u\\n\", 0 SYNTH_DICT_LIST(X), SYNTH_DATA_GOFAKEIT_SHA,\n"
					"\t\t\td->n, d->max_len, d->off[d->n]);\n"
					"\tfor (uint32_t i = 0; i < d->n; i++) {\n"
					"\t\tuint32_t len;\n"
					"\t\tconst char* w = synth_dict_word(d, i, &len);\n"
					"\t\tprintf(\"%u %zu [%s]\\n\", len, strlen(w), w);\n"
					"\t}\n"
					"\treturn 0;\n"
					"}\n")
			exe = os.path.join(tmp, "tiny")
			subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", tmp, "-I", include,
				os.path.join(tmp, "tiny.c"), os.path.join(tmp, "main.c"), "-o", exe],
				check=True, capture_output=True, text=True)
			out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
		total = sum(len(w) + 1 for w in self.WORDS)
		expected = [f"1 {self.GSHA} {len(self.WORDS)} 5 {total}"]
		expected += [f"{len(w)} {len(w)} [{w}]" for w in self.WORDS]
		self.assertEqual(out.splitlines(), expected)


class CheckTest(unittest.TestCase):
	def test_check_detects_drift(self):
		outputs = {"a/x.txt": "hello\n"}
		with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
			self.assertEqual(gen.check(outputs, tmp), 1)
			gen.write(outputs, tmp)
			self.assertEqual(gen.check(outputs, tmp), 0)
			with open(os.path.join(tmp, "a", "x.txt"), "w") as f:
				f.write("hello world\n")
			self.assertEqual(gen.check(outputs, tmp), 1)

	def test_checked_in_files_match(self):
		cached = all(os.path.isdir(os.path.join(gen.DEFAULT_CACHE, sha))
			for sha in (gen.GOFAKEIT_SHA, gen.FAKER_SHA))
		if not cached and not have_network():
			self.skipTest("no upstream cache and no network")
		proc = subprocess.run([sys.executable, SCRIPT, "--check"], capture_output=True, text=True)
		self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
		self.assertIn("up to date", proc.stdout)


if __name__ == "__main__":
	unittest.main()
