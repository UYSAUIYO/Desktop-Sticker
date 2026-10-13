extends RefCounted
## 极简测试框架：用例注册 + 断言 + 汇总。
## 刻意不引 GUT/GdUnit：Studio 的测试要能跑在纯 template_release 运行库上，
## 依赖越少越好，而且引擎是进程级单例，框架必须能跳过"必须独占进程"的用例。
##
## **不要把这个文件改回 static var / static func。** 实测：一个被 preload 的脚本里只要有
## `static var`，进程在引擎 shutdown（Main::cleanup）时就会以 0xC0000374
## （STATUS_HEAP_CORRUPTION）退出 —— 测试全过，退出码却是崩溃码。同样的代码写在
## 主脚本里则没事，改成实例成员也没事。所以状态一律放实例成员，
## 由 run_tests.gd 持有唯一一个实例并传给每个测试文件的 register()。

const EXCLUSIVE_CASES := ["exporter_produces_loadable_pack"]

var _cases: Array = []      # [{name, fn}]
var _failures: Array = []   # ["case: message"]
var _current: String = ""


func case(case_name: String, fn: Callable) -> void:
	_cases.append({"name": case_name, "fn": fn})


func case_names() -> PackedStringArray:
	var out := PackedStringArray()
	for c in _cases:
		out.append(String(c["name"]))
	return out


## only != "" 时只跑那一个用例。run_tests.ps1 分两趟：先跑全量（跳过 EXCLUSIVE_CASES），
## 再用 --only 单跑每个独占用例。
func run_all(only: String) -> int:
	var ran := 0
	for c in _cases:
		var cname := String(c["name"])
		if only != "":
			if cname != only:
				continue
		elif cname in EXCLUSIVE_CASES:
			continue
		_current = cname
		ran += 1
		(c["fn"] as Callable).call()
	var failed := _failures.size()
	for f in _failures:
		printerr("FAIL  ", f)
	print("tests: %d/%d cases run, %d failures" % [ran, _cases.size(), failed])
	return 1 if failed > 0 else 0


## 断开"collector 持有 lambda、lambda 捕获 collector"这个引用环。
## 不调的话引擎退出时会报 "ObjectDB instances were leaked at exit"。
func dispose() -> void:
	_cases.clear()
	_failures.clear()
	_current = ""


func ok(cond: bool, what: String) -> void:
	if not cond:
		_failures.append("%s: %s" % [_current, what])


func eq(actual, expected, what: String) -> void:
	if actual != expected:
		_failures.append("%s: %s (got %s, want %s)" % [_current, what, actual, expected])


func approx(actual: float, expected: float, eps: float, what: String) -> void:
	if absf(float(actual) - float(expected)) > eps:
		_failures.append("%s: %s (got %f, want %f)" % [_current, what, actual, expected])


## 递归删目录。测试收尾用，失败不抛。
func clean_dir(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	for f in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(f))
	for d in DirAccess.get_directories_at(path):
		clean_dir(path.path_join(d))
	DirAccess.remove_absolute(path)
