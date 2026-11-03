"""slim_pose.py <found pose.json> <out json> <picture label>: the shipped pose
(numbers only: no picture, no art), as port/assets/pictures/<bitmap>_<frame>.json
(port/assets/pictures/README.md), which the game embeds and draws the picture from"""
import json
import sys

f = json.load(open(sys.argv[1]))
out = {"picture": sys.argv[3], "scenario": f["scenario"], "bsp": f.get("bsp", 0),
       "aspect": round(f["picture"]["source_aspect"], 4),
       "pose": {k: round(v, 4) for k, v in f["pose"].items() if k != "game_fov"}}
if f.get("objects"):
    out["objects"] = [{k: (round(v, 4) if isinstance(v, float) else v) for k, v in o.items()} for o in f["objects"]]
out["score"] = round(f["score"], 4)
json.dump(out, open(sys.argv[2], "w"), indent=1)
print(json.dumps(out, indent=1))
