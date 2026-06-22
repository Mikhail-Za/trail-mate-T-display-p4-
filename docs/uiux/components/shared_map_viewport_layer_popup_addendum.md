# Shared Map Viewport Layer Popup Addendum

本文件是 [shared_map_viewport.md](shared_map_viewport.md) 与 [shared_map_viewport_impl.md](shared_map_viewport_impl.md) 的最小增补，用于把图层弹窗里的“文案所有权”与“页面壳层职责”彻底切开。

## 1. Distinction

1. 图层弹窗不是某个页面私有的业务对象，而是共享地图图层语义的一个可视化入口。
2. 页面拥有的是“弹窗壳层”。
3. 共享地图组件拥有的是“图层语义与图层专有文案”。

## 2. Component Ownership

共享地图组件必须统一拥有并输出以下地图专有可见语义：

1. 图层弹窗标题键，例如 `Map Layer`。
2. 基础底图名称键，例如 `OSM / Terrain / Satellite`。
3. 状态摘要格式，例如 `Base: <Source>`。
4. 等高线状态文本，例如 `Contour: ON / OFF`。
5. 地图相关缺失提示，例如图层缺失或等高线数据缺失。

页面不允许在自己的文件中重新发明这些字符串，也不允许给同一图层状态起第二套名字。

## 3. Page-Shell Ownership

页面壳层可以独立决定：

1. 弹窗挂载到哪个父容器。
2. 弹窗相对触发按钮或安全区如何定位。
3. 背景遮罩、关闭按钮接线、焦点组切换与退场动画。

但页面壳层不得独立决定：

1. 图层名称。
2. 状态摘要格式。
3. 缺图提示措辞。
4. 图层合法值集合。

## 4. Localization Rule

1. 共享地图组件输出的地图专有文案必须以可本地化 key 或已格式化后的本地化文本形式提供给页面。
2. 页面如果需要显示这些文案，只能消费共享组件给出的 key 或结果，不得把 `Terrain`、`Satellite`、`Contour` 这类词重新写死在页面实现里。
3. 通用动作词例如 `Close` 可以继续走公共 i18n 键，但也不得绕过本地化。

## 5. Consequence

这条增补的目的不是增加抽象层，而是禁止以后再次漂回“页面壳层顺手兼任图层语义拥有者”的影子实现。
