"""Rewrite copies of original bundles into explicit external-MonoScript controls.

Game resources are read only. Output is under Saved/Tests, never abdata/mods.
The source retains a same-PathID decoy class to detect file-scope mistakes.
"""
import hashlib
import json
import zipfile
from pathlib import Path
import UnityPy
from UnityPy.files.SerializedFile import FileIdentifier
from UnityPy.streams import EndianBinaryReader

ROOT = Path(__file__).resolve().parents[2]
GAME = Path("C:/portable/KoikatuSunshine")
OUT = ROOT / "Saved/Tests/ExternalScripts273"


def external(af, name):
    raw = b"\0" + bytes(16) + (0).to_bytes(4, "little") + f"archive:/{name}/{name}".encode() + b"\0"
    return FileIdentifier(af.header, EndianBinaryReader(raw, endian="<"))


def rewrite_refs(value, ids, index):
    if isinstance(value, dict):
        if value.get("m_FileID") == 0 and value.get("m_PathID") in ids:
            value["m_FileID"] = index
        for child in value.values():
            rewrite_refs(child, ids, index)
    elif isinstance(value, (list, tuple)):
        for child in value:
            rewrite_refs(child, ids, index)


def make(label, relative, decoy_class):
    original = GAME / "abdata" / relative
    source = next(iter(UnityPy.load(str(original)).files.values()))
    dependency = next(iter(UnityPy.load(str(original)).files.values()))
    af = next(f for f in source.files.values() if hasattr(f, "externals"))
    df = next(f for f in dependency.files.values() if hasattr(f, "externals"))
    source_name = "CAB-" + hashlib.md5(f"ok273:{label}:source".encode()).hexdigest()
    dep_name = "CAB-" + hashlib.md5(f"ok273:{label}:scripts".encode()).hexdigest()
    classes = {o.path_id: o.read_typetree()["m_ClassName"] for o in af.objects.values() if o.type.name == "MonoScript"}
    ids = set(classes)
    index = len(af.externals) + 1
    af.externals.append(external(af, dep_name))
    decoy = next(i for i, name in classes.items() if name == decoy_class)
    for info in af.script_types:
        if info.local_serialized_file_index == 0 and info.local_identifier_in_file in ids:
            info.local_serialized_file_index = index
    for obj in list(af.objects.values()):
        if obj.type.name == "MonoScript":
            if obj.path_id == decoy:
                tree = obj.read_typetree()
                tree["m_ClassName"] = "DynamicBoneCollider" if decoy_class == "DynamicBone" else "DynamicBone"
                tree["m_Name"] = tree["m_ClassName"]
                obj.save_typetree(tree)
            else:
                del af.objects[obj.path_id]
        elif obj.type.name == "MonoBehaviour" or obj.type.name == "AssetBundle":
            tree = obj.read_typetree()
            rewrite_refs(tree, ids, index)
            if obj.type.name == "AssetBundle":
                tree["m_Name"] = f"ok273/{label}_source.unity3d"
            obj.save_typetree(tree)
    af.name = source_name
    source.files = {source_name if f is af else n: f for n, f in source.files.items()}
    # A dependency bundle exports only the genuine original MonoScript objects.
    for obj in list(df.objects.values()):
        if obj.type.name == "AssetBundle":
            tree = obj.read_typetree()
            tree["m_Name"] = f"ok273/{label}_scripts.unity3d"
            tree["m_PreloadTable"] = [{"m_FileID": 0, "m_PathID": i} for i in sorted(ids)]
            tree["m_Container"] = [(f"scripts/{classes[i]}.cs", {"preloadIndex": j, "preloadSize": 1, "asset": {"m_FileID": 0, "m_PathID": i}}) for j, i in enumerate(sorted(ids))]
            tree["m_MainAsset"] = {"preloadIndex": 0, "preloadSize": 0, "asset": {"m_FileID": 0, "m_PathID": 0}}
            obj.save_typetree(tree)
        elif obj.type.name != "MonoScript":
            del df.objects[obj.path_id]
    df.name = dep_name
    df.script_types = []
    dependency.files = {dep_name: df}
    source_bytes, dep_bytes = source.save("lz4"), dependency.save((66, 2))
    (OUT / f"{label}_source.unity3d").write_bytes(source_bytes)
    (OUT / f"{label}_scripts.unity3d").write_bytes(dep_bytes)
    return {"original": str(original), "source_file": source_name, "dependency_file": dep_name, "external_file_id": index,
            "decoy_id": decoy, "classes": classes, "source_bytes": len(source_bytes), "dependency_bytes": len(dep_bytes)}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "abdata").mkdir(exist_ok=True)
    (OUT / "mods").mkdir(exist_ok=True)
    body = make("body", "chara/oo_base.unity3d", "DynamicBone_Ver02")
    hair = make("hair", "chara/ao_hair_00.unity3d", "DynamicBone")
    # Exercise the production loose-bundle and zipmod dependency locator.
    (OUT / "abdata/body_dependency.unity3d").write_bytes((OUT / "body_scripts.unity3d").read_bytes())
    with zipfile.ZipFile(OUT / "mods/hair_dependency.zipmod", "w", zipfile.ZIP_DEFLATED) as z:
        z.write(OUT / "hair_scripts.unity3d", "abdata/chara/external_scripts.unity3d")
    (OUT / "metadata.json").write_text(json.dumps({"body": body, "hair": hair}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: {n: v for n, v in data.items() if n != "classes"} for k, data in {"body": body, "hair": hair}.items()}, indent=2))


if __name__ == "__main__":
    main()
