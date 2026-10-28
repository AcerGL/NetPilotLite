# 图片资源

- `cover.jpg`：用户提供的原图 A `133683991_p0.jpg`，按原始字节复制嵌入，未重新绘制。显示采用等比例 Fit，不拉伸人物。
- `icon-master.png`：使用 imagegen 技能和用户提供的樱彩图标参考板 B 生成的独立图标，含粉发角色及 RJ45 图形；不是整张设计展板的缩略图。工具模式：参考图编辑；透明背景。
- `NetPilotLite.ico`：原生 C 工具 `tools/icon.c` 从独立图标转换生成，包含 16、24、32、48、64、128、256 像素。16/24 像素采用简化网口符号，保证托盘可读；其他尺寸保持角色图及 Alpha。
- `update-key.bin`：发布验签公钥，非密钥秘密。私钥不随资源分发。

图标生成提示：Create ONE standalone square Windows app icon for NetPilot Lite, using the provided board as the design reference. Output only the independent icon, no board, no captions or text. Match the cute pink-haired character with golden/blue heterochromia, a wink, and a very clear large Ethernet RJ45 symbol on a blue/white badge. Rounded square pastel pink/lavender/blue background, clean bold edges and strong silhouette so it reads at 48px. Make character head/face fill the square with RJ45 foreground bottom-right. No words, no logos other than the Ethernet glyph, no tiny decorative clutter. Transparent outside rounded square. This is an app icon asset, not a screenshot or product presentation.

代码的 MIT 授权不代表第三方插画获得了可对外发行的版权授权。请在公开发行前确认用户提供插画及其衍生图标的使用权。
