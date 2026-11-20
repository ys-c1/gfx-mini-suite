# mini-audio-fifo

交织浮点音频 FIFO（C11）。`make test` 编译并运行测试，`make bench` 跑零拷贝与拷贝读的吞吐对比。

## 拷贝读（原有接口，行为不变）

```c
int fifo_write(AudioFifo* f, const float* interleaved, int frames);
int fifo_read(AudioFifo* f, float* interleaved, int frames);
```

## 零拷贝读

```c
int fifo_read_view(AudioFifo* f, AudioFifoView* view); /* 取视图 */
int fifo_view_commit(AudioFifo* f, int frames);        /* 提交已读帧数 */
```

`fifo_read_view` 返回内部缓冲指针和可读帧数，调用方消费后提交。视图不跨环边界：
只有从 `head` 到缓冲末尾的连续段，因此 `view->frames` 可能小于 `fifo_available()`，
下一帧需要再取一次视图。

### 语义

| 场景 | 行为 |
|---|---|
| 视图有效期内写入 | 允许。写入只落在 `head + count` 之后，不会触碰视图中已占用的帧，指针和帧数保持有效 |
| 视图有效期内拷贝读 | `fifo_read` 返回 -1（它会移动 `head`，使视图失效） |
| 再次取视图 | 返回 -1；同一时刻只允许一个未提交视图 |
| 重复提交 / 无视图时提交 | 返回 -1，不改变状态 |
| 提交 `frames < 0` 或 `frames > view->frames` | 返回 -1，不改变状态，视图仍有效，可用合法值重试 |
| 提交 `frames == 0` | 关闭视图，不消费数据 |
| 部分提交 | 消费前 `frames` 帧，剩余帧留在 FIFO 中供下次视图读取 |
| FIFO 为空 | 返回 0，`view->data == NULL`、`view->frames == 0`，不注册视图 |
| `fifo_clear` / `fifo_free` | 取消未提交视图，未提交的视图数据被丢弃（`clear` 后 FIFO 为空） |

视图指针在 `fifo_view_commit`、`fifo_clear`、`fifo_free` 之前一直有效；FIFO 缓冲区
仅在 `fifo_init` 时分配一次，运行期不重分配。

## 基准

`make bench`（cap 1 MiB 帧、双声道、16K 帧/轮，512 轮）：

```
copy: 50.84 ms  view: 11.90 ms  (4.3x)
```

这里的“拷贝读”包含逐帧的拷贝循环，视图读只做指针交付和提交；实际收益取决于
调用方对样本的处理量。
