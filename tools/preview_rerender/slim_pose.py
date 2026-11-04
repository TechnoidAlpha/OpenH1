"""slim_pose.py <found pose.json> <out json> <picture label> [--colour raw|curve]:
the shipped pose (numbers only: no picture, no art); a pose's own console
lines (prepare), its staged objects and whether they are left as placed
(settle_seconds 0) kept, and which colour treatment the picture takes; as
port/assets/pictures/<bitmap>_<frame>.json, which the game embeds and draws
the picture from (port/assets/pictures/README.md)"""
import json
import sys

f = json.load(open(sys.argv[1]))
out = {"picture": sys.argv[3], "scenario": f["scenario"], "bsp": f.get("bsp", 0),
       "aspect": round(f["picture"]["source_aspect"], 4),
       "pose": {k: round(v, 4) for k, v in f["pose"].items() if k != "game_fov"}}
if f.get("prepare"):
    out["prepare"] = list(f["prepare"])
if f.get("objects"):
    out["objects"] = [{k: (round(v, 4) if isinstance(v, float) else v) for k, v in o.items()} for o in f["objects"]]
if "settle_seconds" in f:
    out["settle_seconds"] = f["settle_seconds"]
if "--colour" in sys.argv:
    out["colour"] = sys.argv[sys.argv.index("--colour") + 1]
score = f.get("score")
if score is None and f.get("final"):
    score = f["final"]["scores"]["colour_curve" if out.get("colour") == "curve" else "raw"]["score"]
if score is not None:
    out["score"] = round(score, 4)
json.dump(out, open(sys.argv[2], "w"), indent=1)
print(json.dumps(out, indent=1))
