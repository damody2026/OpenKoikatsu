-- Builds the comparison backends of Plugins/OKPhysXSecondary from pinned sources and stages headers + libraries into
-- Plugins/OKPhysXSecondary/Source/ThirdParty/{Jolt,MuJoCo} (mujoco.dll into MuJoCo/bin; the build copies it next to the PhysX DLLs):
--   Jolt Physics v5.6.0 (MIT, static /MD)   MuJoCo 3.15.0 (Apache-2.0, DLL)
-- PhysX 5.11 comes from the engine (Engine/Plugins/GameWorks/Flex, Build/PhysX58/Build.ps1 -Stage); KawaiiPhysics v1.21
-- is the plugin source in Plugins/KawaiiPhysics. Bullet is not used by this project.
-- Sources are cloned into Tools/.thirdparty (not in git) at the exact commit below; a different commit stops the build.
-- SIMD level follows UE's Win64 default (no /arch): Jolt gets SSE4.2 (+LZCNT/TZCNT) through defines that the plugin's
-- Build.cs repeats, so the headers and the library agree (Jolt checks this at startup). Needs git, cmake, VS 2022.
--   Tools\lua\lua.exe Tools\build_physics_backends.lua [--clean]   (--clean deletes Intermediate/ThirdPartyBuild first)
local ok = require "ok"
local path = ok.path

local function win(p) return (tostring(p):gsub("/", "\\")) end

-- cmake and git write progress to stderr; failures are caught by exit code.
local function checked(args)
  local res = ok.proc.run(args)
  if not res.ok then error(("%s failed (%s)"):format(args[1], res.error or res.code), 0) end
end

local function main()
  local p = ok.args.parser("Build and stage the Jolt and MuJoCo backends of OKPhysXSecondary")
  p:flag("--clean")
  local a = p:parse(arg)
  local root = win(ok.root)
  local src = root .. "\\Tools\\.thirdparty"
  local work = root .. "\\Intermediate\\ThirdPartyBuild"
  local plugin = root .. "\\Plugins\\OKPhysXSecondary"
  local stage = plugin .. "\\Source\\ThirdParty"
  local sources = {
    { name = "JoltPhysics", url = "https://github.com/jrouwe/JoltPhysics", tag = "v5.6.0", commit = "e77f175595e64cb44218cc9d9d56fc365ad0e36a" },
    { name = "mujoco", url = "https://github.com/google-deepmind/mujoco", tag = "3.15.0", commit = "9ea3cdfcae93bf2cc4dc0e1a1627c5a39a1e06e5" },
  }

  if a.clean then pcall(path.rmtree, work) end
  path.mkdir(src)
  for _, s in ipairs(sources) do
    local dir = src .. "\\" .. s.name
    if not path.exists(dir .. "\\.git") then checked({ "git", "clone", "--depth", "1", "--branch", s.tag, s.url, dir }) end
    local res = ok.proc.run({ "git", "-C", dir, "rev-parse", "HEAD" }, { stdout_only = true })
    local head = ok.util.strip(res.stdout or "")
    if head ~= s.commit then error(("%s: expected %s (%s), found %s. Delete %s to clone again."):format(s.name, s.commit, s.tag, head, dir), 0) end
  end

  -- ---------------------------------------------------------------------------------------------------------------- Jolt
  local j = work .. "\\jolt"
  checked({ "cmake", "-S", src .. "\\JoltPhysics\\Build", "-B", j, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF", "-DINTERPROCEDURAL_OPTIMIZATION=OFF", "-DFLOATING_POINT_EXCEPTIONS_ENABLED=OFF",
    "-DGENERATE_DEBUG_SYMBOLS=OFF", "-DUSE_SSE4_1=ON", "-DUSE_SSE4_2=ON", "-DUSE_AVX=OFF", "-DUSE_AVX2=OFF", "-DUSE_AVX512=OFF",
    "-DUSE_LZCNT=ON", "-DUSE_TZCNT=ON", "-DUSE_F16C=OFF", "-DUSE_FMADD=OFF", "-DENABLE_OBJECT_STREAM=OFF",
    "-DJPH_USE_DX12=OFF", "-DJPH_USE_VK=OFF", "-DJPH_USE_MTL=OFF", "-DJPH_USE_CPU_COMPUTE=OFF", "-DENABLE_ALL_WARNINGS=OFF",
    "-DTARGET_UNIT_TESTS=OFF", "-DTARGET_HELLO_WORLD=OFF", "-DTARGET_PERFORMANCE_TEST=OFF", "-DTARGET_SAMPLES=OFF", "-DTARGET_VIEWER=OFF" })
  checked({ "cmake", "--build", j, "--config", "Distribution", "--parallel", "--target", "Jolt" })
  local js = stage .. "\\Jolt"
  pcall(path.rmtree, js)
  path.mkdir(js .. "\\lib"); path.mkdir(js .. "\\include")
  local lib = path.rglob(j, "Jolt.lib")[1]
  if not lib then error("Jolt.lib not found under " .. j, 0) end
  path.copy(lib, js .. "\\lib\\Jolt.lib")
  local jolt_src = src .. "\\JoltPhysics"
  for _, f in ipairs(path.rglob(jolt_src .. "\\Jolt", "*")) do
    if path.is_file(f) and (path.fnmatch(path.name(f), "*.h") or path.fnmatch(path.name(f), "*.inl")) then
      local dst = js .. "\\include\\" .. win(path.rel(f, jolt_src))
      path.mkdir(path.parent(dst))
      path.copy(f, dst)
    end
  end
  path.copy(jolt_src .. "\\LICENSE", js .. "\\LICENSE")

  -- -------------------------------------------------------------------------------------------------------------- MuJoCo
  -- Shared library (its default): mujoco.dll goes to MuJoCo/bin; OKPhysXSecondary.Build.cs copies it next to the PhysX DLLs.
  local m = work .. "\\mujoco"
  local install = work .. "\\mujoco-install"
  checked({ "cmake", "-S", src .. "\\mujoco", "-B", m, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DMUJOCO_BUILD_EXAMPLES=OFF", "-DMUJOCO_BUILD_SIMULATE=OFF", "-DMUJOCO_BUILD_TESTS=OFF", "-DMUJOCO_TEST_PYTHON_UTIL=OFF",
    "-DCMAKE_INSTALL_PREFIX=" .. install })
  checked({ "cmake", "--build", m, "--config", "Release", "--parallel", "--target", "install" })
  local ms = stage .. "\\MuJoCo"
  pcall(path.rmtree, ms)
  path.mkdir(ms .. "\\lib")
  path.copytree(install .. "\\include", ms .. "\\include")
  path.copy(install .. "\\lib\\mujoco.lib", ms .. "\\lib\\mujoco.lib")
  local bin = ms .. "\\bin"
  path.mkdir(bin)
  path.copy(install .. "\\bin\\mujoco.dll", bin .. "\\mujoco.dll")
  path.copy(src .. "\\mujoco\\LICENSE", ms .. "\\LICENSE")
  path.copy(src .. "\\mujoco\\LICENSE", bin .. "\\MuJoCo-LICENSE.txt")
  print("Staged into " .. stage)
end

ok.main(main)
