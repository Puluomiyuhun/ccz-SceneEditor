# cczEditor2 - 曹操传MOD剧本编辑器

## 项目概述
MFC MDI应用，用于编辑曹操传MOD剧本(.EEX文件)。内置HTTP服务器(端口8080)提供REST API和MCP协议接口。

## MCP 连接
编辑器启动HTTP服务器后，通过 `mcp-bridge.js` 桥接脚本连接。配置见 `.mcp.json`。

### 可用MCP工具
- `get_tree_structure(startOrd?, maxDepth?, maxItems?)` - 读取指令树
- `get_instruction(ord)` - 获取单条指令详情(id/intData/text)
- `add_instruction(afterOrd, instructionId, intData?, text?)` - 添加单条指令
- `add_instructions_batch(afterOrd, instructions)` - **批量添加(推荐，省token)**
- `modify_instruction(ord, intData?, text?)` - 修改指令
- `delete_instruction(ord)` - 删除指令及子节点
- `find_combo_index(listType, name)` - 查人物/物品等索引值
- `get_list_items(listType, count?)` - 获取列表项
- `search_instructions(instructionId?, text?, intDataIndex?, intDataValue?)` - 搜索
- `get_current_selection()` - 获取当前选中项

### 最佳实践
- **批量写入**: 用 `add_instructions_batch` 一次写完整段剧本，避免逐条调用浪费token
- **先查后写**: 先用 `find_combo_index` 查人物索引，再组装instructions JSON
- **了解结构**: 先 `get_tree_structure` 看整体，再精确操作

## 关键源文件
- `cczEditor2/cczEditor2Doc.cpp` - 文档类，HTTP服务器+MCP端点实现
- `cczEditor2/cczEditor2View.cpp` - 树视图，指令操作核心逻辑
- `cczEditor2/ScenePreviewWnd.cpp` - 场景预览面板(可视化编辑)
- `cczEditor2/httplib.h` - HTTP库(已修改为Win7兼容)
- `cczEditor2/json.hpp` - nlohmann JSON库
- `mcp-bridge.js` - MCP stdio桥接脚本

## 剧本结构
```
Root → Scene(id=-1) → Section(id=-2) → 指令(id=0-123)
```
每条指令有: `ord`(唯一序号), `id`(类型), `int_data[20]`(整数参数), `text`(文本)

## 常用指令ID
| ID | 名称 | 关键参数 |
|----|------|----------|
| 2 | 内部信息(注释) | text |
| 5 | 变量测试 | intData[0-24]=变量列表 |
| 9 | 延时 | intData[0]=毫秒 |
| 11(0xb) | 变量赋值 | intData[0]=变量ID, [1]=值 |
| 18(0x12) | 选择框 | text=选项(\n分隔) |
| 20(0x14) | 对话 | intData[0]=人物索引, text="&人名\n对话" |
| 48(0x30) | 武将出现 | intData[0]=人物,[1]=X,[2]=Y,[3]=方向,[4]=动作 |
| 49(0x31) | 武将消失 | intData[0]=人物 |
| 50(0x32) | 武将移动 | intData[0]=人物,[1]=X,[2]=Y |
| 51(0x33) | 武将转向 | intData[0]=人物,[1]=方向(0北1东2南3西) |
| 68(0x44) | 战斗初始化 | intData[0]=地图ID |
| 71(0x47) | 敌军出场设定 | 含人物/坐标/方向/等级 |
| 105(0x69) | 旁白 | text |
| 118(0x76) | 无条件跳转 | intData[0]=目标ord |
| 119(0x77) | 变量运算 | intData[0]=变量,[1]=运算符,[4]=值 |

## listType 对照
person/item/direction/job/magic/skill/face/movie/object/terrain/camp/gesture/weather/policy/condition/var_kind/compare/operate

## 编译
```bash
MSBuild cczEditor2.sln -p:Configuration=Release -p:Platform=x86
```
输出: `Release/s.exe`

## 注意事项
- 文本参数用UTF-8编码(编辑器内部自动转换)
- intData最多20个元素
- 操作前需确保编辑器已打开剧本文件且HTTP服务器已启动
- Win7兼容: httplib.h已将Win8+ API替换为经典API
