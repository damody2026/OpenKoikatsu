/* okfs: UTF-8 path/process primitives for the vendored Lua 5.4 on Windows.
 * lua54.dll links msvcrt.dll, so every FILE* handed to Lua comes from
 * msvcrt's own _wfopen/_wpopen (resolved at load), never this module's CRT. */
#define LUA_LIB
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "lua.h"
#include "lauxlib.h"

typedef FILE *(__cdecl *wfopen_t)(const wchar_t *, const wchar_t *);
typedef int (__cdecl *fclose_t)(FILE *);
typedef int (__cdecl *wsystem_t)(const wchar_t *);
static wfopen_t crt_wfopen, crt_wpopen;
static fclose_t crt_fclose, crt_pclose;
static wsystem_t crt_wsystem;

static wchar_t *widen(lua_State *L, const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (n <= 0) luaL_error(L, "okfs: invalid UTF-8 string");
  wchar_t *w = (wchar_t *)lua_newuserdatauv(L, (size_t)n * sizeof(wchar_t), 0);
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static void push_narrow(lua_State *L, const wchar_t *w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = (char *)malloc((size_t)n);
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  lua_pushstring(L, s);
  free(s);
}

static int win_error(lua_State *L, const char *what) {
  DWORD code = GetLastError();
  wchar_t buf[512];
  luaL_pushfail(L);
  if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, buf, 512, NULL)) {
    push_narrow(L, buf);
    lua_pushfstring(L, "%s: %s", what, lua_tostring(L, -1));
    lua_remove(L, -2);
  } else lua_pushfstring(L, "%s: error %d", what, (int)code);
  lua_pushinteger(L, (lua_Integer)code);
  return 3;
}

static int close_file(lua_State *L) {
  luaL_Stream *p = (luaL_Stream *)luaL_checkudata(L, 1, LUA_FILEHANDLE);
  errno = 0;
  return luaL_fileresult(L, crt_fclose(p->f) == 0, NULL);
}

static int close_pipe(lua_State *L) {
  luaL_Stream *p = (luaL_Stream *)luaL_checkudata(L, 1, LUA_FILEHANDLE);
  return luaL_execresult(L, crt_pclose(p->f));
}

static luaL_Stream *new_stream(lua_State *L) {
  luaL_Stream *p = (luaL_Stream *)lua_newuserdatauv(L, sizeof(luaL_Stream), 0);
  p->closef = NULL;
  luaL_setmetatable(L, LUA_FILEHANDLE);
  return p;
}

/* open(path [, mode]) -> file | fail, msg, errno */
static int l_open(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  luaL_argcheck(L, strspn(mode, "rwa+bt") == strlen(mode) && mode[0], 2, "invalid mode");
  wchar_t *wpath = widen(L, path), *wmode = widen(L, mode);
  luaL_Stream *p = new_stream(L);
  errno = 0;
  p->f = crt_wfopen(wpath, wmode);
  if (!p->f) return luaL_fileresult(L, 0, path);
  p->closef = close_file;
  return 1;
}

/* popen(cmd [, mode]) -> file ; close() returns exit status like io.popen */
static int l_popen(lua_State *L) {
  const char *cmd = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  luaL_argcheck(L, (mode[0] == 'r' || mode[0] == 'w'), 2, "invalid mode");
  wchar_t *wcmd = widen(L, cmd);
  wchar_t wmode[4] = {(wchar_t)mode[0], L'b', 0, 0};
  luaL_Stream *p = new_stream(L);
  fflush(NULL);
  p->f = crt_wpopen(wcmd, wmode);
  if (!p->f) return luaL_fileresult(L, 0, cmd);
  p->closef = close_pipe;
  return 1;
}

static int l_execute(lua_State *L) {
  const char *cmd = luaL_optstring(L, 1, NULL);
  if (!cmd) { lua_pushboolean(L, 1); return 1; }
  fflush(NULL);
  return luaL_execresult(L, crt_wsystem(widen(L, cmd)));
}

static int l_remove(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  wchar_t *w = widen(L, path);
  DWORD attr = GetFileAttributesW(w);
  BOOL ok = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? RemoveDirectoryW(w) : DeleteFileW(w);
  if (!ok) return win_error(L, path);
  lua_pushboolean(L, 1);
  return 1;
}

/* rename(a, b [, replace]) ; replace=true overwrites like Path.replace */
static int l_rename(lua_State *L) {
  const char *a = luaL_checkstring(L, 1), *b = luaL_checkstring(L, 2);
  DWORD flags = MOVEFILE_COPY_ALLOWED | (lua_toboolean(L, 3) ? MOVEFILE_REPLACE_EXISTING : 0);
  if (!MoveFileExW(widen(L, a), widen(L, b), flags)) return win_error(L, a);
  lua_pushboolean(L, 1);
  return 1;
}

static int l_copy(lua_State *L) {
  const char *a = luaL_checkstring(L, 1), *b = luaL_checkstring(L, 2);
  if (!CopyFileW(widen(L, a), widen(L, b), lua_toboolean(L, 3))) return win_error(L, a);
  lua_pushboolean(L, 1);
  return 1;
}

static int l_getenv(lua_State *L) {
  wchar_t *name = widen(L, luaL_checkstring(L, 1));
  DWORD n = GetEnvironmentVariableW(name, NULL, 0);
  if (n == 0) { luaL_pushfail(L); return 1; }
  wchar_t *buf = (wchar_t *)lua_newuserdatauv(L, (size_t)n * sizeof(wchar_t), 0);
  GetEnvironmentVariableW(name, buf, n);
  push_narrow(L, buf);
  return 1;
}

/* setenv(name, value|nil): affects child processes started afterwards */
static int l_setenv(lua_State *L) {
  wchar_t *name = widen(L, luaL_checkstring(L, 1));
  wchar_t *value = lua_isnoneornil(L, 2) ? NULL : widen(L, luaL_checkstring(L, 2));
  if (!SetEnvironmentVariableW(name, value)) return win_error(L, "setenv");
  /* keep the CRT copy (used by _wsystem/_wpopen children) in sync */
  typedef int (__cdecl *wputenv_t)(const wchar_t *, const wchar_t *);
  static wputenv_t put;
  if (!put) put = (wputenv_t)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "_wputenv_s");
  if (put) put(name, value ? value : L"");
  lua_pushboolean(L, 1);
  return 1;
}

static int l_sleep(lua_State *L) {
  Sleep((DWORD)(luaL_checknumber(L, 1) * 1000.0));
  return 0;
}

/* perf_counter(): monotonic seconds */
static int l_clock(lua_State *L) {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
  lua_pushnumber(L, (lua_Number)c.QuadPart / (lua_Number)f.QuadPart);
  return 1;
}

/* time(): unix seconds with sub-second precision */
static int l_time(lua_State *L) {
  FILETIME ft; ULARGE_INTEGER u;
  GetSystemTimePreciseAsFileTime(&ft);
  u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
  lua_pushnumber(L, (lua_Number)(u.QuadPart - 116444736000000000ULL) / 1e7);
  return 1;
}

/* stat(path) -> {size, mtime, is_dir} | nil ; mtime in unix seconds (float) */
static int l_stat(lua_State *L) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(widen(L, luaL_checkstring(L, 1)), GetFileExInfoStandard, &d)) { luaL_pushfail(L); return 1; }
  ULARGE_INTEGER t; t.LowPart = d.ftLastWriteTime.dwLowDateTime; t.HighPart = d.ftLastWriteTime.dwHighDateTime;
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, ((lua_Integer)d.nFileSizeHigh << 32) | d.nFileSizeLow); lua_setfield(L, -2, "size");
  lua_pushnumber(L, (lua_Number)(t.QuadPart - 116444736000000000ULL) / 1e7); lua_setfield(L, -2, "mtime");
  lua_pushboolean(L, (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0); lua_setfield(L, -2, "is_dir");
  return 1;
}

/* fullpath(path) -> absolute normalized path */
static int l_fullpath(lua_State *L) {
  wchar_t *w = widen(L, luaL_checkstring(L, 1));
  DWORD n = GetFullPathNameW(w, 0, NULL, NULL);
  if (!n) return win_error(L, "fullpath");
  wchar_t *buf = (wchar_t *)lua_newuserdatauv(L, (size_t)n * sizeof(wchar_t), 0);
  GetFullPathNameW(w, n, buf, NULL);
  push_narrow(L, buf);
  return 1;
}

/* utf8_console(): make print() of UTF-8 text readable in the console. The
 * console code pages are shared with the parent shell, so the originals are
 * restored when the process exits. */
static UINT saved_out_cp, saved_in_cp;
static void restore_console(void) {
  if (saved_out_cp) SetConsoleOutputCP(saved_out_cp);
  if (saved_in_cp) SetConsoleCP(saved_in_cp);
}
static int l_utf8_console(lua_State *L) {
  (void)L;
  if (!saved_out_cp) {
    saved_out_cp = GetConsoleOutputCP();
    saved_in_cp = GetConsoleCP();
    if (!saved_out_cp) return 0; /* no console attached */
    atexit(restore_console);
  }
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  return 0;
}

/* ---- Interactive child processes with pipes (subprocess.Popen) ---- */
static BOOL opt_bool(lua_State *L, int opts, const char *field);
#define PROC_MT "okfs.proc"
typedef struct {
  HANDLE process, in_w, out_r;
  DWORD pid;
  char *buf; size_t len, cap;
  int eof, exited; DWORD code;
} Proc;

static Proc *check_proc(lua_State *L) { return (Proc *)luaL_checkudata(L, 1, PROC_MT); }

static void buf_need(Proc *p, size_t extra) {
  if (p->len + extra <= p->cap) return;
  size_t cap = p->cap ? p->cap : 4096;
  while (cap < p->len + extra) cap *= 2;
  p->buf = (char *)realloc(p->buf, cap); p->cap = cap;
}

/* Pull available output; wait up to ms (INFINITE blocks until data/EOF). */
static int proc_fill(Proc *p, DWORD ms) {
  if (p->eof || !p->out_r) return 0;
  DWORD avail = 0, start = GetTickCount();
  for (;;) {
    if (!PeekNamedPipe(p->out_r, NULL, 0, NULL, &avail, NULL)) { p->eof = 1; return 0; }
    if (avail > 0) break;
    if (ms != INFINITE && GetTickCount() - start >= ms) return -1;
    Sleep(1);
  }
  buf_need(p, avail);
  DWORD got = 0;
  if (!ReadFile(p->out_r, p->buf + p->len, avail, &got, NULL) || got == 0) { p->eof = 1; return 0; }
  p->len += got;
  return 1;
}

static void push_take(lua_State *L, Proc *p, size_t n) {
  lua_pushlstring(L, p->buf, n);
  memmove(p->buf, p->buf + n, p->len - n); p->len -= n;
}

/* popen2(cmdline [, {cwd=, stdout=path|"NUL"|nil(pipe), stderr="stdout"|path|nil(inherit), hide=}]) -> proc */
static int l_popen2(lua_State *L) {
  const char *cmd = luaL_checkstring(L, 1);
  int opts = lua_istable(L, 2) ? 2 : 0;
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  HANDLE in_r, in_w, out_r = NULL, out_w, herr = INVALID_HANDLE_VALUE;
  const char *out_file = NULL;
  if (opts) { lua_getfield(L, opts, "stdout"); out_file = lua_tostring(L, -1); lua_pop(L, 1); }
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) return win_error(L, "pipe");
  if (out_file) {
    /* stdout to a file (or NUL): not readable from Lua, never blocks the child */
    out_w = CreateFileW(widen(L, out_file), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    lua_pop(L, 1);
    if (out_w == INVALID_HANDLE_VALUE) { CloseHandle(in_r); CloseHandle(in_w); return win_error(L, out_file); }
  } else if (!CreatePipe(&out_r, &out_w, &sa, 0)) { CloseHandle(in_r); CloseHandle(in_w); return win_error(L, "pipe"); }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  if (out_r) SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r; si.hStdOutput = out_w;
  const char *err = NULL; const char *cwd = NULL; BOOL hide = FALSE;
  if (opts) {
    lua_getfield(L, opts, "stderr"); err = lua_tostring(L, -1); lua_pop(L, 1);
    lua_getfield(L, opts, "cwd"); cwd = lua_tostring(L, -1); lua_pop(L, 1);
    hide = opt_bool(L, opts, "hide");
  }
  if (err && strcmp(err, "stdout") == 0) si.hStdError = out_w;
  else if (err) {
    herr = CreateFileW(widen(L, err), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    lua_pop(L, 1);
    si.hStdError = herr;
  } else {
    DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &herr, 0, TRUE, DUPLICATE_SAME_ACCESS);
    si.hStdError = herr;
  }
  if (hide) { si.dwFlags |= STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; }
  wchar_t *wcwd = cwd ? widen(L, cwd) : NULL;
  wchar_t *wcmd = widen(L, cmd);
  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT, NULL, wcwd, &si, &pi);
  DWORD gle = GetLastError();
  CloseHandle(in_r); CloseHandle(out_w);
  if (herr != INVALID_HANDLE_VALUE && herr) CloseHandle(herr);
  if (!ok) { CloseHandle(in_w); if (out_r) CloseHandle(out_r); SetLastError(gle); return win_error(L, cmd); }
  CloseHandle(pi.hThread);
  Proc *p = (Proc *)lua_newuserdatauv(L, sizeof(Proc), 0);
  memset(p, 0, sizeof(*p));
  p->process = pi.hProcess; p->in_w = in_w; p->out_r = out_r; p->pid = pi.dwProcessId;
  luaL_setmetatable(L, PROC_MT);
  return 1;
}

/* proc:write(s) -> true | nil, msg */
static int proc_write(lua_State *L) {
  Proc *p = check_proc(L);
  size_t n; const char *s = luaL_checklstring(L, 2, &n);
  if (!p->in_w) return luaL_error(L, "stdin closed");
  while (n > 0) {
    DWORD put = 0;
    if (!WriteFile(p->in_w, s, (DWORD)n, &put, NULL)) return win_error(L, "write");
    s += put; n -= put;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int proc_close_stdin(lua_State *L) {
  Proc *p = check_proc(L);
  if (p->in_w) { CloseHandle(p->in_w); p->in_w = NULL; }
  return 0;
}

/* proc:read_line([timeout_s]) -> line without "\n" (CR stripped) | nil,"timeout" | nil,"eof" */
static int proc_read_line(lua_State *L) {
  Proc *p = check_proc(L);
  DWORD ms = lua_isnoneornil(L, 2) ? INFINITE : (DWORD)(luaL_checknumber(L, 2) * 1000.0);
  DWORD start = GetTickCount();
  for (;;) {
    char *nl = p->len ? (char *)memchr(p->buf, '\n', p->len) : NULL;
    if (nl) {
      size_t n = (size_t)(nl - p->buf);
      size_t keep = (n > 0 && p->buf[n - 1] == '\r') ? n - 1 : n;
      lua_pushlstring(L, p->buf, keep);
      memmove(p->buf, nl + 1, p->len - n - 1); p->len -= n + 1;
      return 1;
    }
    if (p->eof) {
      if (p->len) { push_take(L, p, p->len); return 1; }
      luaL_pushfail(L); lua_pushstring(L, "eof"); return 2;
    }
    DWORD left = INFINITE;
    if (ms != INFINITE) { DWORD el = GetTickCount() - start; if (el >= ms) { luaL_pushfail(L); lua_pushstring(L, "timeout"); return 2; } left = ms - el; }
    proc_fill(p, left);
  }
}

/* proc:read([n|"a"] [, timeout_s]) -> up to n bytes (blocks for at least one) or all until EOF */
static int proc_read(lua_State *L) {
  Proc *p = check_proc(L);
  DWORD ms = lua_isnoneornil(L, 3) ? INFINITE : (DWORD)(luaL_checknumber(L, 3) * 1000.0);
  if (lua_type(L, 2) == LUA_TSTRING) {
    while (!p->eof) proc_fill(p, INFINITE);
    push_take(L, p, p->len);
    return 1;
  }
  size_t want = (size_t)luaL_optinteger(L, 2, 65536);
  if (p->len == 0 && !p->eof && proc_fill(p, ms) < 0) { luaL_pushfail(L); lua_pushstring(L, "timeout"); return 2; }
  if (p->len == 0) { luaL_pushfail(L); lua_pushstring(L, "eof"); return 2; }
  push_take(L, p, want < p->len ? want : p->len);
  return 1;
}

/* proc:wait([timeout_s]) -> exit code | nil,"timeout" ; proc:poll() -> code | nil */
static int proc_wait(lua_State *L) {
  Proc *p = check_proc(L);
  if (!p->exited) {
    DWORD ms = lua_isnoneornil(L, 2) ? INFINITE : (DWORD)(luaL_checknumber(L, 2) * 1000.0);
    if (WaitForSingleObject(p->process, ms) == WAIT_TIMEOUT) { luaL_pushfail(L); lua_pushstring(L, "timeout"); return 2; }
    GetExitCodeProcess(p->process, &p->code); p->exited = 1;
  }
  lua_pushinteger(L, (lua_Integer)(int)p->code);
  return 1;
}
static int proc_poll(lua_State *L) {
  lua_settop(L, 1); lua_pushnumber(L, 0);
  int n = proc_wait(L);
  if (n == 2) { lua_pushnil(L); return 1; }
  return n;
}

static int proc_kill(lua_State *L) {
  Proc *p = check_proc(L);
  if (!p->exited) TerminateProcess(p->process, 1);
  return 0;
}

static int proc_pid(lua_State *L) { lua_pushinteger(L, (lua_Integer)check_proc(L)->pid); return 1; }

static int proc_gc(lua_State *L) {
  Proc *p = check_proc(L);
  if (p->in_w) { CloseHandle(p->in_w); p->in_w = NULL; }
  if (p->out_r) { CloseHandle(p->out_r); p->out_r = NULL; }
  if (p->process) { CloseHandle(p->process); p->process = NULL; }
  free(p->buf); p->buf = NULL;
  return 0;
}

static const luaL_Reg proc_methods[] = {
  {"write", proc_write}, {"close_stdin", proc_close_stdin}, {"read_line", proc_read_line},
  {"read", proc_read}, {"wait", proc_wait}, {"poll", proc_poll}, {"kill", proc_kill},
  {"pid", proc_pid}, {"__gc", proc_gc}, {"__close", proc_gc}, {NULL, NULL}
};

static int l_args(lua_State *L) {
  /* Raw UTF-8 command line arguments (lua.exe receives them in the ANSI code page). */
  int n = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &n);
  lua_createtable(L, n, 0);
  for (int i = 0; i < n; i++) { push_narrow(L, argv[i]); lua_rawseti(L, -2, i); }
  LocalFree(argv);
  return 1;
}

static HANDLE open_redirect(lua_State *L, int opts, const char *field, BOOL write, BOOL append) {
  lua_getfield(L, opts, field);
  const char *p = lua_tostring(L, -1);
  lua_pop(L, 1);
  if (!p) return INVALID_HANDLE_VALUE;
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  wchar_t *w = widen(L, p);
  HANDLE h = CreateFileW(w, write ? (append ? FILE_APPEND_DATA : GENERIC_WRITE) : GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                         write ? (append ? OPEN_ALWAYS : CREATE_ALWAYS) : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  lua_pop(L, 1);
  if (h == INVALID_HANDLE_VALUE) luaL_error(L, "okfs.run: cannot open %s for %s", p, field);
  return h;
}

static BOOL opt_bool(lua_State *L, int opts, const char *field) {
  lua_getfield(L, opts, field);
  BOOL v = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return v;
}

/* run(cmdline [, {stdin=path, stdout=path, stderr=path|"stdout", cwd=path,
 *     append=bool, wait=bool(default true), hide=bool, timeout=seconds}])
 * -> exit code (wait) | pid (no wait) ; nil, msg on failure to start.
 * CreateProcessW directly: no cmd.exe, so arguments need only MSVC quoting. */
static int l_run(lua_State *L) {
  const char *cmd = luaL_checkstring(L, 1);
  int opts = lua_istable(L, 2) ? 2 : 0;
  if (!opts) { lua_newtable(L); opts = lua_gettop(L); }
  BOOL append = opt_bool(L, opts, "append");
  lua_getfield(L, opts, "wait");
  BOOL wait = lua_isnil(L, -1) ? TRUE : lua_toboolean(L, -1);
  lua_pop(L, 1);
  HANDLE hin = open_redirect(L, opts, "stdin", FALSE, FALSE);
  HANDLE hout = open_redirect(L, opts, "stdout", TRUE, append);
  lua_getfield(L, opts, "stderr");
  BOOL err_to_out = lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "stdout") == 0;
  lua_pop(L, 1);
  HANDLE herr = err_to_out ? INVALID_HANDLE_VALUE : open_redirect(L, opts, "stderr", TRUE, append);
  STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
  BOOL redirect = hin != INVALID_HANDLE_VALUE || hout != INVALID_HANDLE_VALUE || herr != INVALID_HANDLE_VALUE || err_to_out;
  if (redirect) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE nul_in = INVALID_HANDLE_VALUE;
    if (hin == INVALID_HANDLE_VALUE) {
      /* keep the parent's stdin when inheritable, else NUL */
      HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
      if (h && h != INVALID_HANDLE_VALUE && DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &nul_in, 0, TRUE, DUPLICATE_SAME_ACCESS)) hin = nul_in;
      else hin = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    }
    HANDLE dout = INVALID_HANDLE_VALUE, derr = INVALID_HANDLE_VALUE;
    if (hout == INVALID_HANDLE_VALUE) DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), GetCurrentProcess(), &dout, 0, TRUE, DUPLICATE_SAME_ACCESS);
    if (herr == INVALID_HANDLE_VALUE && !err_to_out) DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &derr, 0, TRUE, DUPLICATE_SAME_ACCESS);
    si.hStdInput = hin;
    si.hStdOutput = hout != INVALID_HANDLE_VALUE ? hout : dout;
    si.hStdError = err_to_out ? si.hStdOutput : (herr != INVALID_HANDLE_VALUE ? herr : derr);
    if (hout == INVALID_HANDLE_VALUE) hout = dout;
    if (herr == INVALID_HANDLE_VALUE) herr = derr;
  }
  if (opt_bool(L, opts, "hide")) { si.dwFlags |= STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; }
  lua_getfield(L, opts, "cwd");
  const char *cwd = lua_tostring(L, -1);
  wchar_t *wcwd = cwd ? widen(L, cwd) : NULL;
  wchar_t *wcmd = widen(L, cmd); /* CreateProcessW may modify the buffer: userdata copy is fine */
  fflush(NULL);
  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, redirect, CREATE_UNICODE_ENVIRONMENT, NULL, wcwd, &si, &pi);
  DWORD gle = GetLastError();
  if (hin != INVALID_HANDLE_VALUE) CloseHandle(hin);
  if (hout != INVALID_HANDLE_VALUE) CloseHandle(hout);
  if (herr != INVALID_HANDLE_VALUE) CloseHandle(herr);
  if (!ok) { SetLastError(gle); return win_error(L, cmd); }
  CloseHandle(pi.hThread);
  if (!wait) { lua_pushinteger(L, (lua_Integer)pi.dwProcessId); CloseHandle(pi.hProcess); return 1; }
  lua_getfield(L, opts, "timeout");
  DWORD ms = lua_isnumber(L, -1) ? (DWORD)(lua_tonumber(L, -1) * 1000.0) : INFINITE;
  if (WaitForSingleObject(pi.hProcess, ms) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    luaL_pushfail(L); lua_pushstring(L, "timeout"); return 2;
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  lua_pushinteger(L, (lua_Integer)(int)code);
  return 1;
}

static const luaL_Reg funcs[] = {
  {"run", l_run}, {"popen2", l_popen2},
  {"open", l_open}, {"popen", l_popen}, {"execute", l_execute}, {"remove", l_remove},
  {"rename", l_rename}, {"copy", l_copy}, {"getenv", l_getenv}, {"setenv", l_setenv},
  {"sleep", l_sleep}, {"clock", l_clock}, {"time", l_time}, {"stat", l_stat},
  {"fullpath", l_fullpath}, {"utf8_console", l_utf8_console}, {"args", l_args}, {NULL, NULL}
};

LUALIB_API int luaopen_okfs(lua_State *L) {
  HMODULE crt = GetModuleHandleA("msvcrt.dll");
  if (!crt) crt = LoadLibraryA("msvcrt.dll");
  crt_wfopen = (wfopen_t)GetProcAddress(crt, "_wfopen");
  crt_wpopen = (wfopen_t)GetProcAddress(crt, "_wpopen");
  crt_fclose = (fclose_t)GetProcAddress(crt, "fclose");
  crt_pclose = (fclose_t)GetProcAddress(crt, "_pclose");
  crt_wsystem = (wsystem_t)GetProcAddress(crt, "_wsystem");
  if (!crt_wfopen || !crt_wpopen || !crt_fclose || !crt_pclose || !crt_wsystem) return luaL_error(L, "okfs: msvcrt entry points missing");
  if (luaL_newmetatable(L, PROC_MT)) {
    luaL_setfuncs(L, proc_methods, 0);
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
  }
  lua_pop(L, 1);
  luaL_newlib(L, funcs);
  return 1;
}
