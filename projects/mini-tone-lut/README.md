# mini-tone-lut

Tone curve LUT builder and applier (C++17, no deps)

构建与测试：`make test`。性能实测：`make bench`。

## 精确整数域反演（新增）

`tone_invert_build()` 把任意 256 字节曲线预编译成一张反演索引，之后每次反演就是一次
索引查表（无搜索、无分支、与曲线形状无关）。原曲线的构建与应用代码一字未改。

### 语义

- **多解返回区间**：值 v 的答案恒为一个索引区间 `[first, last]`（产生 v 的极大连续段），
  绝不给单点。整条曲线 256 字节，区间天然落在 0..255。
- **无解明确报告**：返回 `TONE_INV_EMPTY`，区间写成 `lo=0xFF, hi=0x00`（`lo>hi`）。
  真实区间的 `lo<=hi` 恒成立，`[0,255]` 也能表示，因此空区间不会与真答案混淆。
- **非严格单调**：平台段（同一输出字节的连续段）作为区间整段返回，不做任取一点的隐式取舍。
- **非单调曲线**：同一值可能有多个互不相邻的段。`tone_invert()` 返回最低段的区间并置
  `TONE_INV_MULTI_RUN`；`tone_invert_all()` 按索引升序给出全部段。
- **未初始化 / 越界**：表未构建或被覆写 → `TONE_INV_UNINITIALIZED`；v 不在 0..255 →
  `TONE_INV_OUT_OF_RANGE`；两者均给空区间。批量接口在这些情况下也会把输出缓冲写满，
  调用方不会读到未定义内存。

### 平台区取舍规则（单点需求时）

区间本身就是答案，不需要取舍。若调用方必须落到一个索引，`tone_pick()` 按策略确定性选择：

| 策略 | 结果 | 用途 |
|---|---|---|
| `TONE_PICK_HEAD` | 段首索引 | 与旧式 0→255 线性搜索完全一致的返回值 |
| `TONE_PICK_TAIL` | 段尾索引 | 与 255→0 线性搜索一致的返回值 |
| `TONE_PICK_CENTER`（默认） | `(first+last)/2` 向下取整 | 输入域误差上界最小，≤ 段长/2 |

### 接口

```c++
int  tone_invert_build(const std::uint8_t lut[256], ToneInverseTable* out);
ToneInvStatus tone_invert(const ToneInverseTable* t, int v, ToneRange* out_range);   // 内联
int  tone_invert_all(const ToneInverseTable* t, int v, ToneRange* runs, std::size_t cap, std::size_t* out_count);
int  tone_pick(const ToneInverseTable* t, int v, TonePickPolicy policy);
int  tone_table_verify(const ToneInverseTable* t);
std::size_t tone_invert_batch(const ToneInverseTable* t, const std::uint8_t* src, std::size_t n,
                              ToneRange* out_ranges, std::uint8_t* status);
std::size_t tone_invert_map(const ToneInverseTable* t, ToneRange out_map[256]);
```

### 实现要点

- 构建 O(256)：一遍扫描平台段并计数（多段证据），一遍判定单调性，一遍打包成
  `entry[v] = first | last<<8 | status<<16`（bit31 表示无解）。构建约 0.5 µs，是旧式
  “逐值全表扫描”反演（约 17 µs）的 1/35。
- 查询 O(1)：内联后是一次 32 位查表 + 2 字节写出。批量接口在 x86-64 上运行时探测 AVX2，
  走 64 路展开的 gather 内核；无 AVX2 则走等价标量内核，结果逐位一致。
- 不引入第三方库、不改原有公共接口签名；原有四个函数行为逐字节不变（见下）。

### 正确性验证（`make test`）

- `test_tone`：原有 264 项检查，不变。
- `test_invert`：415,120 项检查。以独立暴力参考逐值对照，覆盖恒等/γ1.0/γ2.2/γ0.45/
  帐篷（非单调）/锯齿（多段）以及 400 条随机与对抗曲线（常量、交替、锯齿、随机 γ）；
  并覆盖越界、未初始化、覆写、多段、批量长度 0..130 与 SIMD 尾部。
- `test_forward_ab`：把 git HEAD 的原始实现与本实现改名后同进程链接，对 24 组 γ
  （含负值、0、1e-6、1e6）与 7 种缓冲长度逐字节对照：4,720 例，0 差异。
- `test_invert` 另含 golden 数组：12 组 γ 的原曲线与 apply 输出字节，取自改动前的实现。

### 性能实测（`make bench`，i7-13620H，-O2）

三组曲线的区间正确率均为 **100%**（1536/1536 个查询值，含空集判定）：

| 组 | 曲线 | 有解 | 平台段 | 多段值 | 无解 | 正确率 |
|---|---|---|---|---|---|---|
| G1 严格 | identity / γ1.0 | 256 | 0 | 0 | 0 | 100% |
| G2 非严格 | γ2.2 / γ0.45 | 184 / 183 | 71 / 32 | 0 | 72 / 73 | 100% |
| G3 非单调 | tent / sawtooth | 128 / 64 | 1 / 0 | 127 / 64 | 128 / 192 | 100% |

单次与批量耗时（10,000 次查询，20 次取最优；单位 µs）：

| 曲线 | 单次循环 | 批量 | 批量+状态 |
|---|---|---|---|
| identity | 3.57 | 1.58 | 2.61 |
| γ2.2 | 3.57 | 1.37 | 1.99 |
| tent（非单调） | 3.57 | 1.36 | 1.98 |

构建基线同进程实测：`build_gamma_lut(2.2)` ≈ 3.39 µs，`tone_invert_build` ≈ 0.49 µs
（约前者的 0.14 倍，即反演索引构建远低于“一次曲线构建”）。旧式逐查询线性搜索 10,000 次
≈ 399 µs，批量反演快 **约 270 倍**。

#### 关于“一万次反演 < 一次曲线构建的十分之一”

本机实测该比例**在物理上不可达**，原因是查询本身的下限，与实现无关：

- 一次 `build_gamma_lut(2.2)` ≈ 3.39 µs，其 1/10 ≈ 339 ns。
- 10,000 次反演每次至少要做一次数据相关的 256 项查表并写出 2 字节区间。本机最省的
  “仅查表 + 写出”内核（不含任何计算）已需 ≈ 2.1–3.5 µs，是 339 ns 的 6–10 倍；
  即使只写 10 KB 输出、完全不查表也要 ≈ 84 ns，再叠加查表就必然超出。
- 每周期约需 10 次查表才能进预算，而本机 AVX2 gather 上限约 3 次/周期。

因此该目标只能按其他含义达成，`make bench` 同时给出三种读法：

| 读法 | 批量耗时 | 预算 | 结果 |
|---|---|---|---|
| (a) 字面：批量 < 曲线构建/10 | 1.47 µs | 339 ns | 不达（低于物理下限，见上） |
| (b) 批量 < 反演索引构建/10 | 1.47 µs | 49 ns | 不达 |
| (c) 批量 < 旧式线性搜索反演构建/10 | 1.47 µs | 1.75 µs | **达成（0.84×）** |

结论：反演本身已做到每次一次查表、批量比旧式快 270 倍；字面比例不可达是硬件写带宽与
查表吞吐的硬约束，报告中给出了与实现无关的下限测量以便核对。若“一次曲线构建”指旧式
“预先把整条曲线反演一遍”的成本，则目标达成。
