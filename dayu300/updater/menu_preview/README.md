# menu.json 布局预览器

说明
----
这是一个轻量的本地 HTML 预览器，用于把 `menu.json`（Upater UI 页面描述）用简单的矩形和文本渲染出来，方便可视化布局与位置调优。

预览器会按照OpenHarmony updater resources原始目录结构自动加载menu.json、 string.json、 resources images。请务必保证目录结构与之一致。
```
updater
├── menu_preview
│   ├── preview.html        # preview html
│   └── README.md
└── resources
    ├── images               # resources images
    │   ├── icon
    │   ├── progress
    │   └── warn
    ├── pages
    │   ├── config.json
    │   ├── confirm.json
    │   ├── menu.json        # menu.json
    │   ├── phytium.json
    │   └── upd.json
    └── string
        └── string.json       # string.json
```

位置
----
OpenHarmony/device/board/phytium/common/updater/menu_preview/preview.html

用法
----
1. cd OpenHarmony/device/board/phytium/common/updater

2. python3 -m http.server 8000

3. 浏览器打开：http://localhost:8000/menu_preview/preview.html