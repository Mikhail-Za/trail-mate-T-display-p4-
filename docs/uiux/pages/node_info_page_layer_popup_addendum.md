# Node Info Layer Popup Addendum

本文件是 [node_info_page.md](node_info_page.md) 的增补约束，只收敛 `Node Info` 页中的图层弹窗，不重新定义整页布局。

## 1. Scope

1. 本增补只约束 `Node Info` 页底部 `Layer` 按钮打开的图层弹窗。
2. 页面壳层可以决定弹窗出现位置、承载容器、关闭触发方式与焦点切换。
3. 页面壳层不拥有图层语义本身，不得重新定义图层名称、状态摘要或缺图提示。

## 2. Copy Contract

1. `Node Info` 页图层弹窗中的地图专有文案必须复用共享地图组件语义，不得在页面内直接写死另一套英文或页面私有叫法。
2. 至少以下文本必须与共享地图组件保持同义且可本地化：
   - `Map Layer`
   - `Base: <Source>`
   - `OSM / Terrain / Satellite`
   - `Contour: ON / OFF`
   - 图层缺失提示
   - 等高线数据缺失提示
3. 通用关闭动作允许继续复用全局公共文案键，例如 `Close`，但仍必须经过 i18n 处理。

## 3. Small-Screen Operability

1. 在 `320x240` 基线下，图层弹窗首次打开时必须保证 `OSM / Terrain / Satellite / Contour / Close` 五个动作项一次完整可见。
2. `Close` 动作项必须完整可见、完整可点，不允许被屏幕边缘、遮罩、父容器或超高按钮裁切。
3. 如果小屏空间不足，优先收紧弹窗内部间距、按钮高度与定位策略，不允许把最后一个动作项挤成半截露出。

## 4. Consequence

1. 以后如果 `GPS / 地图` 页调整图层名称、摘要格式或缺图提示，`Node Info` 页必须跟随共享语义一起调整。
2. 以后如果 `Node Info` 页只改了弹窗外壳而没有改共享语义层，则视为合法页面实现。
