# Desktop Sticker 验收清单

> 使用方式：手动运行程序后逐项验证，每项完成后勾选。
> 程序启动前请确认 `DesktopSticker.Features.dll` 已位于 EXE 同目录（Release 构建会自动复制，见工程 PostBuildEvent）。

## 自动单元测试

- [ ] `DesktopSticker.Tests.exe` 运行结果：`13 passed, 0 failed`

```bash
"D:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "D:/project/Desktop Sticker/Desktop Sticker/Desktop Sticker.sln" \
  -p:Configuration=Release -p:Platform=x64 -m:1
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release"
cp DesktopSticker.Features.dll Tests/   # 如已自动复制可跳过
./Tests/DesktopSticker.Tests.exe
```

## 桌面分区收纳

- [ ] 首次启动自动创建分区（应用/文档/图片/视频/音乐/文件夹/其他）并收纳桌面图标
- [ ] 分区折叠 / 展开正常
- [ ] 拖拽磁贴换分区正常
- [ ] 拖拽磁贴出分区可恢复为原生图标
- [ ] 从桌面拖入原生图标会被收纳为磁贴
- [ ] 分区右键菜单：打开 / 移出 / 重命名 / 删除
- [ ] 分区可以自由移动、调整大小
- [ ] 双击桌面空白：隐藏全部分区（干净桌面）
- [ ] 再次双击桌面空白：恢复分区

## 搜索启动器

- [ ] 双击空格唤起搜索窗口
- [ ] 再次双击空格隐藏搜索窗口
- [ ] 输入文件名可搜索到桌面 / 文档 / 下载 / 图片 / 视频 / 音乐内容
- [ ] 中文拼音首字母搜索可用（如 `wx` → 微信）
- [ ] 选中结果按 Enter 或点击可打开
- [ ] 在文本输入框中双击空格不会误触发

## 设置与托盘

- [ ] 托盘图标存在，右键菜单可用
- [ ] 设置窗口可打开
- [ ] 搜索范围开关（桌面 / 常用目录）生效
- [ ] 热键方案可切换
- [ ] 手动添加应用后可在搜索中命中
- [ ] 托盘“恢复桌面”可还原所有原生图标
- [ ] 托盘“退出”后无残留分区窗口，原生图标全部恢复

## 数据与异常

- [ ] `%APPDATA%\DesktopSticker\` 下生成 `config.json` / `apps.json` / `layout.json`
- [ ] 删除/损坏配置文件后程序可自动重建，不崩溃

## 多显示器（如有条件）

- [ ] 分区分区坐标在副屏上正确
- [ ] 切换显示器分辨率后分区不消失或可恢复
