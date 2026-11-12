# 讀懂這份期中專案：建議閱讀順序

不要一開始從 `server.c` 第一行一路硬看到底。建議照下面順序理解。

## 1. 先懂最小主流程

Server：

```text
socket()
  ↓
bind()
  ↓
listen()
  ↓
accept()
  ↓
fork()
  ↓
Child: recv_line() → 處理 request → send_line()
Parent: close(client_fd) → 回 accept()
```

Client：

```text
socket()
  ↓
connect()
  ↓
send_line(name)
  ↓
recv_line(Hello)
  ↓
send_line(command)
  ↓
recv_line(response)
```

## 2. 為什麼 Parent / Child 都要 close fd？

`fork()` 後 Parent 與 Child 都會得到 file descriptor 的副本。

- Parent：保留 `server_fd`，關 `client_fd`。
- Child：保留 `client_fd`，關 `server_fd`。

Child 不負責接受新 client；Parent 不負責和既有 client 對話。

## 3. `libcommon.so` 在做什麼？

`common.c` 裡的函式同時被 Client 和 Server 使用：

- `send_all()`：處理 `write()` 可能只寫一部分資料。
- `send_line()`：送一整行，最後補 `\n`。
- `recv_line()`：安全收一整行並限制 buffer 大小。
- `make_greeting()`：建立 `Hello, xxx!`。
- `LOGD()` / debug functions：共用 debug 機制。

編譯後：

```text
common.c → libcommon.so
              ↑       ↑
           server   client
```

## 4. `recv_line()` 為什麼不是直接 `read(fd, buf, nread)`？

它是 robustness 的重點之一。

固定大小陣列如果只有 64 bytes，卻把 200 bytes 直接讀進去，會造成 buffer overflow。`recv_line()` 只寫入 buffer 能容納的部分；如果一整行太長，剩餘資料仍會從 socket 讀掉，但不寫進 buffer，最後回傳 `-2`。

## 5. System information 從哪裡來？

本版本刻意不用 `popen(client_input)`，避免 command injection。

- `uname()` → hostname / kernel / architecture
- `sysinfo()` → uptime / memory
- `statvfs()` → disk
- `time()` / `localtime_r()` → server time

## 6. Debug 有哪兩層？

### Compile time

```bash
make debug
```

會加：

```text
-DENABLE_DEBUG
```

所以 `LOGD()` 真正存在。

```bash
make release
```

沒有 `-DENABLE_DEBUG`，`LOGD()` macro 會變成什麼都不做。

### Runtime

Debug build 下：

```bash
./server --debug=0
./server --debug=1
./server --debug=2
./server --debug=3
```

數字越高顯示越多。

## 7. Signal 要懂哪兩個？

### SIGCHLD

Child `exit()` 後 kernel 通知 Parent。Parent 的 handler 用：

```c
waitpid(-1, NULL, WNOHANG)
```

把已結束 child 回收，避免 zombie。

### SIGPIPE

如果對方已經關閉 socket，你還 `write()`，可能收到 SIGPIPE。Robust mode 會 ignore SIGPIPE，再用 `write()` 的 return value 判斷失敗。

## 8. 三個 robustness demo 怎麼講？

1. **Unknown command**：`robust=0` 關連線；`robust=1` 回錯誤後可繼續。
2. **Idle client**：`robust=0` child 可一直 block；`robust=1` 15 秒 timeout。
3. **Too-long input**：final `recv_line()` 不會把超過 buffer 的資料寫進陣列，可偵測後回錯誤。

## 9. 為什麼 `listen(..., 20)` 不是只能服務 20 人？

`20` 是 backlog，表示尚未被 `accept()` 取走的 pending connections 排隊空間，不是已經 `accept()+fork()` 後同時服務的 client 上限。

## 10. 上台前至少要能自己回答

- `socket / bind / listen / accept` 各做什麼？
- `fork()` parent / child 回傳值？
- 為什麼 Parent/Child 要各自 close 不需要的 fd？
- `SIGCHLD` 與 zombie 是什麼？
- `.so` 為什麼執行時還要找得到？`LD_LIBRARY_PATH` 做什麼？
- `-DENABLE_DEBUG` 與 `--debug=2` 有什麼不同？
- `recv_line()` 怎麼避免名字太長造成 buffer overflow？
- `listen()` backlog 與「10 concurrent clients」有什麼不同？
- tcpdump 裡 SYN / SYN-ACK / ACK 是哪三包？
