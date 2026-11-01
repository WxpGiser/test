# ShpToTiles — SHP → 3D Tiles 导入插件（Cesium for Unreal）

一个 UE 5.8 编辑器插件：在可停靠面板里选择 Shapefile，按属性字段拉伸成体块，写出 **3D Tiles 1.0 (b3dm)** 数据集，并自动以 `Cesium3DTileset` Actor 加入当前关卡。

- 可选 **每个要素贴地**：逐个要素向场景里的 Cesium 地形 tileset 采样高度
- 可选 **按 SHP 属性字段着色**：唯一值分类 / 数值分级，7 套内置色带
- 依赖：`CesiumForUnreal`（本工程 v2.30.0）

---

## 1. 打开面板

- 菜单：`Tools（工具）` → `SHP / 3D Tiles` → `SHP → 3D Tiles 导入…`
- 控制台：`ShpToTiles.Open`

面板是普通 Nomad 页签，可以拖进任意 Dock 区。

## 2. 使用流程

| 步骤 | 操作 |
|---|---|
| 1 | **SHP 文件** → `浏览…` 选中 `.shp`（同目录的 `.dbf` / `.prj` 会被自动读取） |
| 2 | 点 **读取字段 / 坐标系** → 下方显示几何类型、记录数、字段数、编码、.prj 解析结果 |
| 3 | **高度字段** 选层数字段（如 `floor`），**单位高度(m)** 填 3.0（层高）；或高度字段选 `(常量高度)` 用固定高度 |
| 4 | **分块 N×N**：把数据切成 N×N 个 b3dm 瓦片（数据量大时调大，默认 16） |
| 5 | **贴地**：勾选 `每个要素贴合 Cesium 地形` → 逐要素采样地形高度；不勾选则用 `基准高程(m)` |
| 6 | **按属性着色**：着色方式 / 着色字段 / 调色板 / 分级数 |
| 7 | 点 **生成并加入场景**，进度条与日志会显示解析 → 采样 → 建模 → 写出 → 加入场景 |
| 8 | **定位到数据**：把视口飞到数据范围；**打开输出目录**：打开 `tileset.json` 所在目录 |

## 3. 参数说明

### 几何 / 拉伸

| 参数 | 说明 |
|---|---|
| 高度字段 | 参与拉伸的数值字段。高度 = 字段值 × 单位高度。留空/选「常量高度」则用常量高度 |
| 单位高度(m) | 每单位字段值对应的高度，中国建筑轮廓数据常见 `floor`（层数），层高 3 m |
| 常量高度(m) | 无高度字段时的统一高度 |
| 最小面积(m²) | 占地投影面积小于该值的环会被丢弃（清除碎片/错误环），默认 8 |
| 分块 N×N | 经纬度网格数量，每格一个 `tile_XX_YY.b3dm` |
| 双面 | 墙面额外补一层反向面（默认关闭：白模是闭合体块，单面即可） |
| 封底 | 生成底面（默认关闭，贴地时看不见） |
| 无光照 | 用 `KHR_materials_unlit`，专题色不受光照影响（默认关闭，使用 PBR 更立体） |

### 贴地

| 参数 | 说明 |
|---|---|
| 每个要素贴合 Cesium 地形 | 对数据范围的经纬度栅格采样地形高度，再对每个要素做双线性插值，把要素底面放到采样到的椭球高 |
| 基准高程(m) | 不贴地时的底面椭球高；贴地时作为采样失败的回退值 |
| 贴地偏移(m) | 采样高度之上的额外抬高（比如让建筑略高于地表避免 z-fighting） |

采样源自动选择：优先 URL 中含 `terrain` 的 tileset（Cesium World Terrain），否则取关卡里第一个 `Cesium3DTileset`。

采样策略（两段式；栅格 = `分块 N×N / 2`，夹在 3×3 ~ 10×10，默认 16 → 9×9 = 81 个采样点，与要素数量无关）：

1. **先射线（快）**：从每个栅格点上方向下打一条 `ECC_Visibility` 射线，命中当前**已渲染**的地形网格就取交点椭球高。用的是视口里看得见的那层 LOD，不需要等流式加载。
2. **再采样（慢，只补没命中的点）**：对射线没命中的栅格点调用 `ACesium3DTileset::SampleHeightMostDetailed`（会流入 max LOD 地形瓦片），**25 秒超时**；超时则用已采到的部分栅格。
3. 要素按形心在栅格里**双线性插值**；四角落都没数据时取**最近的已采样栅格点**；一个有效点都没有才回退到 `基准高程(m)`。

诊断命令：`ShpToTiles.SampleTest <lon> <lat>` —— 对单个点调一次地形采样并打印回调耗时/结果，用来判断「贴地没生效」是采样慢、还是地形没加载。

实测（武汉全城 165,378 要素，俯视视角下地形已流式加载）：射线先命中 11 个点，其余 70 个由采样补齐，**81 / 81 个栅格点全部拿到高程**，`clampedFeatures = 165,378`，底面高程从 25 m 变成地形实际值（`minBaseHeight = 2.40 m`，`maxTopHeight = 215.04 m`）。

> 注意：`SampleHeightMostDetailed` 依赖「这些点上已经有地形瓦片」——相机没看过的区域它会一直等，所以整片数据都在视野内时贴地最稳。射线则依赖地形瓦片有碰撞体（CesiumWorldTerrain 默认有）。

### 按属性着色

| 参数 | 说明 |
|---|---|
| 着色方式 | `单色白模` / `唯一值分类`（如用途、区县）/ `数值分级`（如层数、面积） |
| 着色字段 | 来自 `.dbf` 的任意字段 |
| 调色板 | Viridis / Spectral / Terrain / Blues / Warm / Categorical / Gray |
| 分级数 | 数值分级的级数（2–32） |

颜色写入 glTF 顶点色 `COLOR_0`（`VEC4` ubyte，线性空间），因此同一个 b3dm 可以一次画出多种颜色，不需要拆材质。

### 高级：坐标系 / 编码

| 参数 | 说明 |
|---|---|
| 手动坐标参数 | 忽略 `.prj`，按横轴墨卡托（Gauss-Kruger）解释坐标 |
| 中央经线(°)、假东(m)、比例因子 k0 | 手动 TM 参数。3 度带常用 `CM=114`、`FE=500000`、`k0=1` |
| DBF 编码 | 自动检测 / GBK·CP936 / UTF-8（中文属性表多数是 GBK） |

坐标系支持范围：
- `.prj` 为 `GEOGCS`（经纬度）→ 直接使用
- `.prj` 为 `PROJCS`、投影为 `Transverse_Mercator` / `Gauss_Kruger` → 反算经纬度（Snyder 级数，厘米级）
- 缺少 `.prj` 且坐标值 < 360 → 按 WGS84 经纬度处理；坐标值是投影大数 → 会报错并提示勾选「手动坐标参数」
- 带号坐标（如 `40500000`）会自动剔除带号前缀

## 4. 输出结构

```
<输出目录>/
├── tileset.json          # 3D Tiles 1.0，root 用 region + children(box+transform+content)
├── meta.json             # 本次生成的统计（要素数、瓦片数、三角形数、字段、坐标系…）
└── tiles/
    ├── tile_00_00.b3dm
    └── ...
```

- 每个瓦片 = 一个 b3dm（28 字节头 + `{"BATCH_LENGTH":0}` feature table + GLB）
- 瓦片变换矩阵（16 doubles，列主序）三列依次是 **ECEF 的 east / north / up 单位向量**，平移是瓦片原点 ECEF：
  `[east, 0, north, 0, up, 0, origin, 1]`
- GLB 内容帧仍是 glTF 规定的 **Y-up**：`(x, y, z) = (东, 上, -北)`。运行时 Cesium 会先做 `Y_UP_TO_Z_UP`（x, -z, y → 得到 (东, 北, 上)），再乘上面的变换 —— 两边约定必须成对，否则整片模型会绕东轴转 90°（楼"躺"在地上）
- 对应地 `boundingVolume.box` 用**变换后的**瓦片帧 `(东, 北, 上)` 写，不要用 GLB 的 (东, 上, -北)
- **不要**在 `asset` 里加 `"gltfUpAxis":"Z"`：加了之后运行时不再做 Y-up 转换，反而会二次旋转
- root 的 `boundingVolume` 是 `region`（radians + 椭球高）

## 5. 脚本 / 命令行

面板之外还有两条等价的自动化入口（都走同一个 `FShpBuildJob`）：

```text
# 控制台命令
ShpToTiles.Build D:/data/wuhan/武汉.shp -out D:/out/wuhan -height floor -floor 3 -grid 16 -color "区县" -palette Spectral -noadd
ShpToTiles.Build D:/data/wuhan/武汉.shp -clamp -offset 0.5
ShpToTiles.Status
```

Python（编辑器内）：

```python
import unreal, json
opts = json.loads(unreal.ShpToTilesLibrary.get_default_options_json())   # 输出是 camelCase 键
opts.update({
    "shpPath": r"D:\work\data\shp\武汉建筑轮廓数据\武汉.shp",
    "outDir":  r"D:\work\ue\CesiumForUnrealSamples-v2.30.0\Saved\ShpToTiles\wuhan",
    "heightField": "Floor", "floorHeight": 3.0,          # 字段名 = .dbf 里的字段名
    "gridSize": 16, "minFootprintArea": 8.0,
    "colorMode": "Classify", "colorField": "Floor", "palette": "Viridis", "numClasses": 5,
    "bClampToGround": False, "baseHeight": 25.0,
    "bAddToScene": True,
})
print(unreal.ShpToTilesLibrary.build_from_json(json.dumps(opts)))
print(unreal.ShpToTilesLibrary.get_status_json())
```

> 采样贴地阶段需要游戏线程逐帧推进，因此脚本里不要用 `time.sleep` 阻塞；发起任务后让编辑器正常 tick，稍后再调 `get_status_json()`。

### 在编辑器里用 Python 驱动时的几个坑（实测）

- `Cesium3DTileset` 的 URL 没有 `get_url()`，要 `get_editor_property("url")`
- `LevelEditorSubsystem.set_level_viewport_camera_info` 少了第 3 个参数，用 `unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)`
- `CesiumGeoreference` 的 `scale` 默认是 100（1 米 = 100 uu），算相机偏移时别按米直接填
- 瓦片只有在编辑器视口真正渲染时才会流式加载；编辑器窗口最小化/被遮挡时 `CesiumWorldTerrain` 也可能 0 个瓦片组件

## 6. 已知取舍

- **先定位 Georeference，再导入**：`Cesium3DTileset` 的「tileset → Unreal」变换在 actor 创建时确定，导入后再大幅移动 `CesiumGeoreference` 原点可能出现位置偏移；重新生成一次（或重设一次 `url`）即可恢复
- 重复导入同一个数据集会自动改名（`Shp3DTiles_武汉`、`Shp3DTiles_武汉_2`…），不会互相覆盖
- 只做**拉伸体块**（墙体 + 屋顶），不做平铺专题面、不做 LOD 分层简化
- 屋顶三角化用耳切 + 洞桥接，支持带庭院（内环）的要素；极端自交/退化环会退化成扇形三角化
- 每个要素用自身 ENU 框架精确落位，瓦片变换只做最后一次刚体变换，避免大范围平面近似误差
- 生成的 b3dm 关闭了物理碰撞（`SetCreatePhysicsMeshes(false)`），需要碰撞可自行在 Actor 上改
- 5 万+ 要素、1 万瓦片量级时建议分块 ≥ 16，观测内存占用（顶点数据为 float32）

## 7. 排障

| 症状 | 原因 / 处理 |
|---|---|
| 下拉框里显示红色 `No Content Provided`，选了下拉项也没变化 | `SComboBox` 必须显式提供 `.Content()`：不提供时 Slate 会用它内置的红色警告文本当按钮内容，选中项不会显示（本插件 v1 曾踩过，已修） |
| 下拉里只有「(常量高度)」一项 | 字段列表没读：确认 `.shp` 同目录有 `.dbf`；路径用「浏览…」选取或输完回车（提交即重读） |
| 高度字段选 `OBJECTID` / `Id` 后楼高得离谱 | 那些是编号不是层数，高度 = 字段值 × 单位高度；改用 `Floor` 或「(常量高度)」 |
| 生成后没有任何颜色 | 「着色方式」必须选「数值分级」或「唯一值分类」，只填着色字段不生效 |
| 贴地没效果 | 见第 3 节：地形瓦片要在视野内加载过、瓦片要有碰撞体；日志里会写「射线命中 N 个 / 有效采样点 N」 |

诊断命令：`ShpToTiles.Panel`（打印面板当前状态，含下拉项与当前选中项）、`ShpToTiles.Panel setheight <字段>`。

## 8. 实测数据（武汉建筑轮廓，165,378 个要素）

| 指标 | 值 |
|---|---|
| 输入 | `D:\work\data\shp\武汉建筑轮廓数据\武汉.shp`（Polygon、166,062 记录、字段 OBJECTID/Id/Floor/Shape_Leng/Shape_Area、GEOGCS WGS84） |
| 有效要素 | 165,378（跳过 2,062 个面积过小/退化） |
| 输出 | 127 个 b3dm 瓦片、2,398,846 三角形、149 MB |
| 耗时 | 解析 0.2 s、建模 0.5 s、写出 0.7 s，总计 **约 13 s**（不含贴地采样） |
| 着色 | 按 `Floor` 数值分级 5 级（1 ~ 68 层），Viridis 顶点色 |
| 不贴地 | `Saved/ShpToTiles/wuhan/`：底面统一 25 m，`maxTopHeight = 229 m` |
| 贴地 | `Saved/ShpToTiles/wuhan_clamped/`：81/81 栅格点采样成功，底面取地形高程（`minBaseHeight = 2.40 m`），`maxTopHeight = 215.04 m` |
| 截图 | `Saved/ShpToTiles/screenshots/overview_wuhan.png`、`closeup_wuhan.png`、`panel_shptotiles.png` |
