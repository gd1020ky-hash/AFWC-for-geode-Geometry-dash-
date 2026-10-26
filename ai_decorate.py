#!/usr/bin/env python3
"""Optional real-AI helper for Frame Forge. Python 3.9+; standard library only.
Requires a running Ollama service and an ALREADY INSTALLED model. Downloads no
model, uploads no level, and uses no paid API. The model chooses positions/scales
for glow objects, not unrestricted GD objects or arbitrary commands.

Example:
  python ai_decorate.py --model YOUR_INSTALLED_MODEL --prompt "neon waves" \
      --x-start 600 --x-end 3480 --safe-y 270
Copy the resulting ai-plan.json into Frame Forge's mod save directory, then
choose Import AI plan in the editor. This does NOT promise the best decoration,
collision freedom in an existing level, or playability. Back up and inspect it.
"""
import argparse
import json
import math
import os
from pathlib import Path
import tempfile
import unittest
import urllib.error
import urllib.parse
import urllib.request

MAX_POINTS = 200
MAX_REPLY = 2 * 1024 * 1024

def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("Duplicate JSON key: " + key)
        result[key] = value
    return result

def _constant(value):
    raise ValueError("Non-finite JSON constant: " + value)

def strict_loads(text):
    return json.loads(text, object_pairs_hook=_pairs, parse_constant=_constant)

def number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(label + " must be a number, not a boolean")
    try:
        result = float(value)
    except (ValueError, OverflowError) as error:
        raise ValueError(label + " is not finite") from error
    if not math.isfinite(result):
        raise ValueError(label + " is not finite")
    return result

def validate_bounds(x_start, x_end, safe_y):
    a = number(x_start, "x-start")
    b = number(x_end, "x-end")
    y = number(safe_y, "safe-y")
    if not 0 <= a < b <= 250000 or not 240 <= y <= 1200:
        raise ValueError("Require 0 <= x-start < x-end <= 250000 and 240 <= safe-y <= 1200")
    return a, b, y

def validate_response(value, x_start, x_end, safe_y):
    a, b, floor = validate_bounds(x_start, x_end, safe_y)
    if not isinstance(value, dict) or set(value) != {"decorations"}:
        raise ValueError("Model must return exactly one object with a decorations key")
    points = value["decorations"]
    if not isinstance(points, list) or not 1 <= len(points) <= MAX_POINTS:
        raise ValueError("Require 1..200 decorations")
    clean = []
    for i, point in enumerate(points):
        if not isinstance(point, dict) or set(point) != {"x", "y", "scale"}:
            raise ValueError("Each decoration must contain only x, y and scale")
        x, y, scale = (number(point[k], "point %d %s" % (i, k)) for k in ("x", "y", "scale"))
        if not a <= x <= b or not floor <= y <= 1200 or not .25 <= scale <= 2:
            raise ValueError("Decoration %d is outside the allowed bounds" % i)
        clean.append({"x": x, "y": y, "scale": scale})
    return clean

def generate(base_url, model, prompt, x_start, x_end, safe_y, seed=None):
    validate_bounds(x_start, x_end, safe_y)
    parsed = urllib.parse.urlsplit(base_url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("Ollama URL must be an HTTP(S) endpoint without credentials")
    if not model.strip() or len(model) > 256 or not prompt.strip() or len(prompt) > 4000:
        raise ValueError("Supply a model name and a prompt of 1..4000 characters")
    schema = {
        "type": "object", "additionalProperties": False,
        "properties": {"decorations": {"type": "array", "minItems": 1, "maxItems": MAX_POINTS,
            "items": {"type": "object", "additionalProperties": False,
                "properties": {"x": {"type": "number", "minimum": x_start, "maximum": x_end},
                    "y": {"type": "number", "minimum": safe_y, "maximum": 1200},
                    "scale": {"type": "number", "minimum": .25, "maximum": 2}},
                "required": ["x", "y", "scale"]}}}, "required": ["decorations"]}
    instruction = (
        "Design a visually coherent decorative arrangement for a Geometry Dash editor draft. "
        "You control positions and scales of circular glow object 211 ONLY. Return JSON only, "
        "matching the supplied schema. Do not return executable code, triggers, or object IDs. "
        "Use at most 200 points. x must be between %s and %s, y between %s and 1200, "
        "scale between 0.25 and 2. Do not claim guaranteed quality or playability. "
        "Style request: %s" % (x_start, x_end, safe_y, prompt))
    options = {"temperature": 0, "num_predict": 8192}
    if seed is not None:
        options["seed"] = seed
    payload = json.dumps({"model": model, "prompt": instruction, "stream": False,
                          "format": schema, "options": options}, allow_nan=False).encode("utf-8")
    endpoint = base_url.rstrip("/") + "/api/generate"
    request = urllib.request.Request(endpoint, data=payload, headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            data = response.read(MAX_REPLY + 1)
    except urllib.error.URLError as error:
        raise ValueError("Ollama request failed. Start Ollama and install/select your own local model: " + str(error)) from error
    if len(data) > MAX_REPLY:
        raise ValueError("Ollama reply exceeds 2 MiB")
    envelope = strict_loads(data.decode("utf-8"))
    if not isinstance(envelope, dict) or "error" in envelope:
        raise ValueError("Ollama returned an error or malformed reply")
    if envelope.get("done") is not True or not isinstance(envelope.get("response"), str):
        raise ValueError("Ollama generation did not finish; try a shorter prompt/fewer decorations")
    points = validate_response(strict_loads(envelope["response"]), x_start, x_end, safe_y)
    return {"version": 1, "model": model, "prompt": prompt, "decorations": points}

def save_plan(plan, destination, overwrite=False):
    path = Path(destination)
    if path.exists() and not overwrite:
        raise ValueError("Output already exists; choose a new --output or explicitly pass --overwrite")
    if not path.parent.is_dir():
        raise ValueError("Output directory does not exist")
    text = json.dumps(plan, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
    if len(text.encode("utf-8")) > 128 * 1024:
        raise ValueError("Plan is too large for the mod")
    handle, temporary = tempfile.mkstemp(prefix=".forge-", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        if overwrite:
            os.replace(temporary, path)
        else:
            # Hard-link publication is atomic and cannot replace an existing path.
            # Use a normal PC filesystem (not a cloud-mounted/FAT destination).
            os.link(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

class ValidationTests(unittest.TestCase):
    def valid(self):
        return {"decorations": [{"x": 600, "y": 270, "scale": .5}]}
    def test_valid(self):
        self.assertEqual(validate_response(self.valid(), 0, 1000, 240)[0]["x"], 600)
    def test_bool(self):
        v = self.valid(); v["decorations"][0]["x"] = True
        with self.assertRaises(ValueError): validate_response(v, 0, 1000, 240)
    def test_finite(self):
        for bad in [float("nan"), float("inf"), 10**1000]:
            v = self.valid(); v["decorations"][0]["scale"] = bad
            with self.assertRaises(ValueError): validate_response(v, 0, 1000, 240)
    def test_bounds(self):
        for field, value in [("x", -1), ("y", 239), ("y", 1201), ("scale", .1), ("scale", 3)]:
            v = self.valid(); v["decorations"][0][field] = value
            with self.assertRaises(ValueError): validate_response(v, 0, 1000, 240)
    def test_extra_keys(self):
        v = self.valid(); v["decorations"][0]["objectID"] = 8
        with self.assertRaises(ValueError): validate_response(v, 0, 1000, 240)
    def test_count(self):
        for points in [[], self.valid()["decorations"] * 201]:
            with self.assertRaises(ValueError): validate_response({"decorations": points}, 0, 1000, 240)
    def test_json(self):
        for text in ['{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}']:
            with self.assertRaises(ValueError): strict_loads(text)
    def test_save_no_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "ai-plan.json"
            plan = {"version": 1, "model": "test", "prompt": "test", **self.valid()}
            save_plan(plan, path)
            with self.assertRaises(ValueError): save_plan(plan, path)
            save_plan(plan, path, overwrite=True)
            self.assertEqual(strict_loads(path.read_text())["version"], 1)

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--model")
    parser.add_argument("--prompt")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    parser.add_argument("--x-start", type=float, default=600)
    parser.add_argument("--x-end", type=float, default=3480)
    parser.add_argument("--safe-y", type=float, default=270)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--output", default="ai-plan.json")
    parser.add_argument("--overwrite", action="store_true", help="explicitly permit replacing the output file")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ValidationTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if not args.model or not args.prompt:
        parser.error("--model and --prompt are required unless using --self-test")
    try:
        if Path(args.output).exists() and not args.overwrite:
            raise ValueError("Output already exists; choose another name or explicitly use --overwrite")
        plan = generate(args.ollama_url, args.model, args.prompt, args.x_start, args.x_end, args.safe_y, args.seed)
        save_plan(plan, args.output, args.overwrite)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(1, "Error: %s\n" % error)
    print("Created %s (%d glow decorations). Inspect in the editor; quality/playability are not guaranteed." %
          (args.output, len(plan["decorations"])))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
