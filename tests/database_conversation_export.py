"""Parse actual explorer exports and verify candidate identity, order and limits."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def load(root, name):
    data = (root / name).read_bytes()
    assert not data.startswith(b"\xef\xbb\xbf"), "Unexpected UTF-8 BOM"
    value = json.loads(data.decode("utf-8"))
    assert value["schema"] == "xfiles-conversation-reference-v1"
    assert value["candidate_only"] is True
    assert value["eligibility_evaluated"] is False
    assert value["name_preview_only"] is True
    assert value["scope"] == "displayed rows for selected source record"
    assert value["display_limit"] == 4096
    for index, row in enumerate(value["rows"]):
        assert row["row_index"] == index
        assert isinstance(row["details"], str)
        assert isinstance(row["unresolved"], bool)
    return value


def candidates(value, role):
    return [r for r in value["rows"] if r["role"].startswith(role + " [")]


def verify(root, installed_count):
    topic = load(root, "dialog.json")
    assert topic["source_record"] == {"class_id": 0x31, "record_id": 301, "offset": 832}
    assert not topic["display_limit_reached"] and not topic["index_incomplete"]
    assert topic["has_unresolved"] and topic["incomplete"]
    assert len(topic["rows"]) == 13
    assert topic["rows"][0]["topic_name_preview"] == "Will you file these APBs?"
    for role in ("Question", "Response"):
        rows = candidates(topic, role)
        assert [r["candidate_record"]["record_id"] for r in rows] == [600, 601, 600, 602, 0, 999]
        assert [r["stored_position"] for r in rows] == list(range(6))
        assert [r["availability"] for r in rows] == [
            "Installed", "Missing file", "Installed", "No movie path", "Null entry", "Missing / unsupported"
        ]
        assert all(r["topic_record"]["record_id"] == 301 for r in rows)
        assert all(r["candidate_list_record"]["record_id"] == 501 for r in rows)
        assert rows[0]["path"] == "xv/19794.xmv"
        assert rows[1]["path"] == "xv/19790.xmv"
        assert all(r["path"] is None for r in rows[3:])
        assert rows[4]["candidate_record"]["record_id"] == 0
        assert rows[4]["candidate_record"]["offset"] is None

    character = load(root, "character.json")
    listing = load(root, "list.json")
    assert character["source_record"]["class_id"] == 0x2D
    assert character["source_record"]["record_id"] == 400
    assert listing["source_record"]["class_id"] == 0x32
    assert listing["source_record"]["record_id"] == 201
    assert character["rows"] == listing["rows"]
    assert len(character["rows"]) == 37
    topics = [r["topic_record"]["record_id"] for r in character["rows"] if r["role"] == "Topic"]
    assert topics == [300, 301, 0, 301], "Null or duplicate association members were lost"
    assert "flashlight" in character["rows"][3]["role"]
    assert "badge click fallback" in character["rows"][5]["role"]
    assert character["rows"][8]["role"] == "Extra stored reply [7]"

    malformed = load(root, "malformed.json")
    assert malformed["incomplete"] and malformed["has_unresolved"]
    assert candidates(malformed, "Question")[0]["availability"] == "Missing / unsupported"
    raw = load(root, "raw-name.json")
    assert raw["rows"][0]["topic_name_preview"] == 'A"\\\\x01\\x80\\xff'
    unicode = load(root, "unicode.json")
    assert unicode["database_path"].endswith("Fran\u00e7ais-\U0001f6f8.HDB")
    assert unicode["rows"][0]["details"] == '"\\\t\r\n\u00e9\U0001f6f8\0end'
    large = load(root, "large-name.json")
    assert "name preview" in large["source_name_preview"]
    assert all(len(r["topic_name_preview"]) < 300 for r in large["rows"])
    limited = load(root, "limited.json")
    assert limited["display_limit_reached"] and limited["incomplete"]
    assert len(limited["rows"]) == 4097
    assert limited["rows"][-1]["role"] == "Display limit"
    assert limited["rows"][-1]["availability"] == "Incomplete"
    exact = load(root, "exact-limit.json")
    assert len(exact["rows"]) == 4096 and not exact["display_limit_reached"]
    assert load(root, "index-incomplete.json")["index_incomplete"]
    assert not (root / "cancel.json").exists()
    assert not (root / "no-selection.json").exists()
    assert not (root / "invalid-unicode.json").exists()
    for index in range(installed_count):
        cook = load(root, f"cook-{index}.json")
        assert cook["source_record"]["record_id"] == 21993
        assert any(r["path"] == "xv/19794.xmv" and "Stock slot [2]" in r["role"] for r in cook["rows"])
        assert any(r["topic_record"] and r["topic_record"]["record_id"] == 58356 for r in cook["rows"])
        assert not cook["display_limit_reached"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("databases", nargs="*")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--wine", action="store_true")
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix="xfiles-reference-export-"))
    output.mkdir(parents=True, exist_ok=True)
    command = (["wine"] if args.wine else []) + [args.executable]
    command.append("Z:" + output.resolve().as_posix() if args.wine else str(output))
    subprocess.run([*command, *args.databases], check=True, timeout=120)
    verify(output, len(args.databases))
    if args.output is None:
        shutil.rmtree(output)
    print("UTF-8 JSON parser, exact candidate order, unresolved records and display-limit checks passed")


if __name__ == "__main__":
    main()
