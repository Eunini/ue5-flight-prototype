#!/usr/bin/env python3
"""Build native editor modules, generate project assets, and optionally package."""
import argparse, json, os, platform, shutil, subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("--engine", required=True, type=Path)
p.add_argument("--lyra", type=Path)
p.add_argument("--package", action="store_true")
p.add_argument("--run", action="store_true")
p.add_argument("--capture", action="store_true")
p.add_argument("--software-renderer", action="store_true")
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
config=json.loads((root/"Tools"/"forge.json").read_text())
engine=a.engine.resolve()
if engine.name=="Engine": engine=engine.parent
if config.get("lyra"):
    if not a.lyra: p.error("--lyra must point to an installed Lyra 5.4 sample")
    project_root=a.lyra.resolve()
    projects=list(project_root.glob("*.uproject"))
    if len(projects)!=1: p.error("The Lyra directory must contain exactly one .uproject")
    project=projects[0]
    for name in ("ArenaMovement","PortfolioForge"):
        source=root/name if name=="ArenaMovement" else root/"Plugins"/name
        dest=project_root/"Plugins"/name
        if dest.exists() and not (dest/".portfolio-managed").exists():
            p.error("Existing plugin "+str(dest)+" must be preserved; use a fresh Lyra copy")
        shutil.copytree(source,dest,dirs_exist_ok=True)
        (dest/".portfolio-managed").touch()
        if name=="ArenaMovement":
            descriptor=dest/"ArenaMovement.uplugin"
            plugin=json.loads(descriptor.read_text())
            plugin["ExplicitlyLoaded"]=False
            plugin["EnabledByDefault"]=True
            descriptor.write_text(json.dumps(plugin,indent=2)+"\n")
    input_config=project_root/"Config"/"DefaultInput.ini"
    old=input_config.read_text() if input_config.exists() else ""
    new=(root/"Tools"/"arena-input.ini").read_text()
    if "ArenaForward" not in old: input_config.write_text(old+"\n"+new)
    (project_root/"Tools").mkdir(exist_ok=True)
    shutil.copy2(root/"Tools"/"forge.json",project_root/"Tools"/"forge.json")
else:
    project_root=root
    project=root/config["project"]
system=platform.system()
target_platform="Win64" if system=="Windows" else ("Mac" if system=="Darwin" else "Linux")
batch=engine/"Engine"/"Build"/"BatchFiles"
build=batch/("Build.bat" if system=="Windows" else ("Mac/Build.sh" if system=="Darwin" else "Linux/Build.sh"))
editor=engine/"Engine"/"Binaries"/target_platform/("UnrealEditor-Cmd.exe" if system=="Windows" else "UnrealEditor-Cmd")
if not editor.is_file(): editor=editor.with_name("UnrealEditor.exe" if system=="Windows" else "UnrealEditor")
if not build.is_file() or not editor.is_file(): p.error("Engine build tools and editor executable were not found")
subprocess.run([str(build),config["target"],target_platform,"Development","-Project="+str(project),"-WaitMutex","-NoHotReload","-NoDebugInfo","-MaxParallelActions=6"],check=True)
subprocess.run([str(editor),str(project),"-run=PortfolioForge","-unattended","-NullRHI","-nosplash","-log"],check=True)
receipt=project_root/"Saved"/"PortfolioAssets.json"
if not receipt.is_file(): raise SystemExit("Editor asset receipt was not produced")
data=json.loads(receipt.read_text())
if not data.get("success"): raise SystemExit("Editor asset generation did not complete")
print("Native assets generated:",len(data["assets"]))
if config.get("lyra"):
    for folder in ("ArenaDemo","Portfolio"):
        source=project_root/"Content"/folder
        if source.exists(): shutil.copytree(source,root/"Content"/folder,dirs_exist_ok=True)

if a.package:
    uat=batch/("RunUAT.bat" if system=="Windows" else "RunUAT.sh")
    subprocess.run([str(uat),"BuildCookRun","-project="+str(project),"-noP4","-platform="+target_platform,
                    "-clientconfig=Development","-nodebuginfo","-ubtargs=-NoDebugInfo -MaxParallelActions=6","-build","-cook","-map="+config["map"],
                    "-stage","-pak","-archive","-archivedirectory="+str(root/"Artifacts"/target_platform)],check=True)
if a.run or a.capture:
    exe=engine/"Engine"/"Binaries"/target_platform/("UnrealEditor.exe" if system=="Windows" else "UnrealEditor")
    cmd=[str(exe),str(project),config["map"],"-game","-windowed","-ResX=1280","-ResY=720","-NoSplash"]
    if a.software_renderer:
        cmd+=["-AllowCPUDevices","-vulkan","-sm5"]
    if a.capture:
        frames=project_root/"Saved"/"PortfolioFrames"
        if frames.exists(): shutil.rmtree(frames)
        capture_receipt=project_root/"Saved"/"PortfolioCapture.json"
        if capture_receipt.exists(): capture_receipt.unlink()
        evidence_receipt=project_root/"Saved"/"GameplayEvidence.json"
        if evidence_receipt.exists(): evidence_receipt.unlink()
        cmd+=["-PortfolioDemo","-PortfolioCapture","-PortfolioFrames="+str(config.get("frames",1350)),"-unattended"]
    subprocess.run(cmd,check=True)


    if a.capture:
        evidence=json.loads(evidence_receipt.read_text())
        if not evidence.get("success"): raise SystemExit("Native gameplay objectives were not completed")
        capture=json.loads(capture_receipt.read_text())
        if not capture.get("success"): raise SystemExit("Native frame capture did not complete")
        output=root/"Artifacts"/(root.name+"-Gameplay.mp4")
        output.parent.mkdir(exist_ok=True)
        subprocess.run(["ffmpeg","-y","-framerate","30","-i",str(frames/"frame-%05d.png"),
                        "-c:v","libx264","-crf","19","-pix_fmt","yuv420p","-movflags","+faststart",str(output)],check=True)
        subprocess.run(["ffmpeg","-v","error","-i",str(output),"-f","null","-"],check=True)
        capture["gameplay"]=evidence
        output.with_suffix(".json").write_text(json.dumps(capture,indent=2)+"\n")
        shutil.rmtree(frames)
        print("Native gameplay recording:",output)
