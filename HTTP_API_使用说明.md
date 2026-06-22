# cczEditor2 HTTP API & MCP 使用说明

## 📖 概述

cczEditor2（曹操传MOD剧本编辑器）内置了 HTTP 服务器，提供两种远程操作方式：

1. **REST API** — 传统HTTP端点，适合Python/JS脚本直接调用
2. **MCP (Model Context Protocol)** — AI专用协议，让 Claude Code / Cursor 等AI工具直接读写剧本

**服务器地址**: `http://localhost:8080`

---

## 🚀 启动/停止服务器

### 方式1：菜单操作
- **启动**: 文件 → 启动HTTP服务器
- **停止**: 文件 → 停止HTTP服务器

### 方式2：API调用
```bash
# 检查服务器状态
curl http://localhost:8080/api/status
```

---

## 🤖 MCP 协议（AI Agent 推荐方式）

### 什么是 MCP？

MCP (Model Context Protocol) 是一个标准化协议，让AI工具能自动发现并调用编辑器的功能。与REST API不同，AI不需要预先知道端点格式，通过 `tools/list` 自动获取所有可用工具和参数定义。

### 配置 Claude Code

在项目目录下创建 `.mcp.json`：

```json
{
  "mcpServers": {
    "cczEditor": {
      "command": "node",
      "args": ["F:\\cczEditor2\\mcp-bridge.js"],
      "env": {
        "CCZ_EDITOR_PORT": "8080"
      }
    }
  }
}
```

配置完成后，Claude Code 会自动启动桥接脚本连接编辑器。

### MCP 工具列表

| 工具名 | 功能 | 典型用途 |
|--------|------|----------|
| `get_tree_structure` | 读取指令树结构 | 浏览剧本整体结构 |
| `get_instruction` | 获取单条指令详情 | 查看指令的参数和文本 |
| `add_instruction` | 添加单条指令 | 快速插入一条指令 |
| `add_instructions_batch` | 批量添加指令(含层级) | **一次写入整段剧本** |
| `modify_instruction` | 修改已有指令 | 更改对话文本、参数 |
| `delete_instruction` | 删除指令(含子节点) | 清理不需要的指令 |
| `find_combo_index` | 查找下拉框索引 | 查曹操/关羽等的数字ID |
| `get_list_items` | 获取列表项 | 查看所有人物/兵种名称 |
| `search_instructions` | 搜索指令 | 按类型/文本/参数查找 |
| `get_current_selection` | 获取当前选中项 | 知道用户选在哪里 |

### MCP 工具详细参数

#### get_tree_structure

获取剧本指令树结构，返回带层级信息的指令列表。

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| startOrd | int | 否 | -1 | 起始节点ord，-1表示从根开始 |
| maxDepth | int | 否 | 3 | 最大递归深度 |
| maxItems | int | 否 | 200 | 最多返回项数 |

**返回示例**:
```json
{
  "items": [
    {"ord": 0, "id": -1, "display": "Scene", "hasChildren": true, "depth": 0},
    {"ord": 1, "id": -2, "display": "Section", "hasChildren": true, "depth": 1},
    {"ord": 2, "id": 2, "display": "2:内部信息 开场剧情", "hasChildren": false, "depth": 2},
    {"ord": 3, "id": 48, "display": "30:武将出现 曹操 (10,20)", "hasChildren": false, "depth": 2}
  ],
  "totalReturned": 4,
  "truncated": false
}
```

#### get_instruction

获取指定ord的指令完整信息。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| ord | int | 是 | 指令的ord值 |

**返回示例**:
```json
{
  "ord": 37,
  "id": 20,
  "typeName": "14:对话 ",
  "intData": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  "text": "&曹操\n诸位将士听令！",
  "display": "14:对话 &曹操\\n诸位将士听令！",
  "parentOrd": 1,
  "hasChildren": false
}
```

#### add_instruction

在指定位置后添加一条新指令。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| afterOrd | int | 是 | 在此ord后面插入，-1表示末尾 |
| instructionId | int | 是 | 指令类型ID (0-123, -1=Scene, -2=Section) |
| intData | int[] | 否 | 整数参数数组(最多20个) |
| text | string | 否 | 文本参数(对话、注释等) |

#### add_instructions_batch

**推荐使用**：一次性写入完整剧本片段，大幅节省AI调用次数。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| afterOrd | int | 是 | 在此ord后面插入 |
| instructions | array | 是 | 指令数组，支持children嵌套 |

**指令对象格式**:
```json
{
  "id": 20,
  "intData": [0],
  "text": "&曹操\n你好！",
  "children": []
}
```

**完整示例**（AI一次调用写入整段对话）:
```json
{
  "afterOrd": 5,
  "instructions": [
    {"id": 2, "text": "=== 曹操出场剧情 ==="},
    {"id": 9, "intData": [50]},
    {"id": 48, "intData": [0, 10, 20, 0, 0]},
    {"id": 20, "intData": [0], "text": "&曹操\n诸位将士，今日我们要攻取汉中！"},
    {"id": 9, "intData": [100]},
    {"id": 20, "intData": [1], "text": "&夏侯惇\n末将愿为先锋！"},
    {"id": 5, "intData": [1900, -1, -1], "children": [
      {"id": 11, "intData": [3500, 1]},
      {"id": 0, "intData": [0]}
    ]}
  ]
}
```

#### modify_instruction

修改已有指令的参数。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| ord | int | 是 | 要修改的指令ord |
| intData | int[] | 否 | 新的整数参数(全量替换) |
| text | string | 否 | 新的文本内容 |

#### delete_instruction

删除指定指令及其所有子节点。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| ord | int | 是 | 要删除的指令ord |

#### find_combo_index

在下拉框列表中查找名称对应的索引。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| listType | string | 是 | 列表类型 |
| name | string | 是 | 要查找的名称 |

**支持的 listType**:

| listType | 说明 | 示例 |
|----------|------|------|
| person | 人物 | "曹操", "夏侯惇" |
| item | 物品 | "倚天剑", "青釭剑" |
| direction | 方向 | "朝北", "朝东", "朝南", "朝西", "默认方向" |
| job | 兵种 | "骑兵", "步兵", "弓兵" |
| magic | 法术 | "火球术", "治疗术" |
| skill | 特技 | "连击", "反击" |
| face | 头像 | "曹操-普通", "曹操-愤怒" |
| movie | 动画 | 动画文件名 |
| object | 物件 | 物件名称 |
| terrain | 地形 | "平原", "树林", "山地" |
| camp | 阵营 | "我军", "友军", "敌军" |
| gesture | 动作 | "普通", "下跪", "攻击" |
| weather | 天气 | "普通", "晴好", "阴雨" |
| policy | 策略 | "被动出击", "主动出击" |
| condition | 条件 | "攻击", "防御", "HP" |
| var_kind | 变量类型 | "指针变量(*p)", "整型变量" |
| compare | 比较符 | ">=", "<", "=" |
| operate | 运算符 | "=", "+", "-" |

#### get_list_items

获取指定列表的前N个项目名称。

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| listType | string | 是 | - | 列表类型(同上) |
| count | int | 否 | 50 | 获取数量 |

#### search_instructions

按条件搜索指令。

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| instructionId | int | 否 | -1 | 按指令类型ID过滤，-1不过滤 |
| text | string | 否 | "" | 按文本内容模糊匹配 |
| intDataIndex | int | 否 | -1 | 检查intData的哪个索引 |
| intDataValue | int | 否 | 0 | 该索引位置的值 |
| maxResults | int | 否 | 50 | 最多返回数量 |

**示例：搜索所有曹操(索引0)的对话(id=20)**:
```json
{"instructionId": 20, "intDataIndex": 0, "intDataValue": 0, "maxResults": 20}
```

#### get_current_selection

获取编辑器中当前选中的指令信息，无需参数。

---

### MCP 使用最佳实践

#### 成本优化：批量写入

**不推荐**（100次工具调用）：
```
AI: add_instruction(afterOrd=5, id=48, ...)
AI: add_instruction(afterOrd=6, id=20, ...)
AI: add_instruction(afterOrd=7, id=9, ...)
... × 100 次
```

**推荐**（1-3次工具调用）：
```
AI: find_combo_index("person", "曹操")     → 得到索引0
AI: find_combo_index("person", "夏侯惇")   → 得到索引1
AI: add_instructions_batch(afterOrd=5, instructions=[... 100条指令的JSON ...])
```

#### 典型AI工作流

1. **了解结构**: `get_tree_structure` → 看剧本整体脉络
2. **查询资源**: `get_list_items("person", 100)` → 获取所有人物
3. **定位插入点**: `search_instructions` 或 `get_instruction` → 找到要编辑的位置
4. **批量写入**: `add_instructions_batch` → 一次性写入完整剧情段落
5. **微调修改**: `modify_instruction` → 修正个别参数

---

## 📡 REST API 端点（传统方式）

### 1. GET /api/status - 获取服务器状态

**请求**:
```bash
curl http://localhost:8080/api/status
```

**响应**:
```json
{
  "status": "running",
  "port": 8080
}
```

---

### 2. POST /api/add_instruction - 添加指令

在指定位置添加新的剧本指令。

**请求体 (JSON)**:
```json
{
  "afterOrd": -1,
  "instructionId": 2,
  "intData": [1, 2, 3],
  "text": "文本内容"
}
```

**参数说明**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| afterOrd | int | 是 | 在ord为多少的指令后面添加，-1表示添加到根节点末尾 |
| instructionId | int | 是 | 指令类型ID (0-123)，见下方指令列表 |
| intData | int[] | 否 | 整数参数数组，最多20个元素 |
| text | string | 否 | 文本参数（UTF-8编码） |

**响应**:
```json
{
  "success": true,
  "message": "指令添加成功"
}
```

---

### 3. POST /api/find_combo_index - 查找下拉框索引

根据名称查找各种下拉框选项的索引值。

**请求体 (JSON)**:
```json
{
  "listType": "person",
  "name": "曹操"
}
```

**响应**:
```json
{
  "index": 0,
  "found": true
}
```

---

### 4. POST /api/get_list_items - 获取列表项

**请求体 (JSON)**:
```json
{
  "listType": "person",
  "count": 50
}
```

---

### 5. POST /api/add_instructions_batch - 批量添加指令

一次性添加多个指令，支持嵌套层级关系（children）。

**请求体 (JSON)**:
```json
{
  "afterOrd": -1,
  "instructions": [
    {"id": 2, "text": "这是一条注释"},
    {"id": 9, "intData": [100]},
    {"id": 5, "intData": [1900, -1, -1], "children": [
      {"id": 20, "intData": [0], "text": "&曹操\n你好！"},
      {"id": 0, "intData": [0]}
    ]}
  ]
}
```

**响应**:
```json
{
  "success": true,
  "message": "批量添加成功",
  "count": 5
}
```

---

## 📋 指令列表 (instructionId)

以下是所有124种指令的ID和名称：

| ID | 名称 | 说明 | 主要参数 |
|----|------|------|----------|
| -1 | Scene | 场景节点 | 无 |
| -2 | Section | 章节节点 | 无 |
| 0 | 事件结束 | 结束当前事件 | 无 |
| 1 | 子事件设定 | 设定子事件 | 无 |
| 2 | 内部信息 | 注释/内部信息 | text |
| 3 | else | 条件分支else | 无 |
| 4 | 询问测试 | 是/否选择 | int_data[0]=1选是/0选否 |
| 5 | 变量测试 | 条件判断 | int_data[0-24]=变量组1, [25-49]=变量组2 |
| 6 | 我军出场限制 | 限制出场 | int_data[0]=bool, int_data[1-10]=人物列表 |
| 7 | 出战测试 | 是否出战 | 无 |
| 8 | 菜单处理 | 显示菜单 | 无 |
| 9 | 延时 | 等待指定时间 | int_data[0]=毫秒数 |
| 10 (0xa) | 初始化局部变量 | 初始化变量 | 无 |
| 11 (0xb) | 变量赋值 | 给变量赋值 | int_data[0]=变量ID, int_data[1]=值 |
| 12 (0xc) | 结束Section | 结束章节 | 无 |
| 13 (0xd) | 结束Scene | 结束场景 | 无 |
| 14 (0xe) | 战斗失败 | 战斗失败处理 | 无 |
| 15 (0xf) | 结局设定 | 设置结局 | 无 |
| 16 (0x10) | 许子将指导 | 指导信息 | 无 |
| 17 (0x11) | 剧本跳转 | 跳转到其他剧本 | 无 |
| 18 (0x12) | 选择框 | 显示选项 | text=选项文本(\\n分隔) |
| 19 (0x13) | case | 选择分支 | int_data[0]=case值 |
| 20 (0x14) | 对话 | 显示对话 | int_data[0]=人物索引, text="&人名\\n对话内容" |
| 21 (0x15) | 对话2 | 显示对话(类型2) | text |
| 22 (0x16) | 信息 | 显示信息 | text |
| 23 (0x17) | 场所名 | 设置场所名 | text |
| 24 (0x18) | 事件名称设定 | 设置事件名 | text |
| 25 (0x19) | 胜利条件 | 设定胜利条件 | 无 |
| 26 (0x1a) | 显示胜利条件 | 显示胜利条件 | 无 |
| 27 (0x1b) | 撤退信息设定 | 撤退信息 | 无 |
| 28 (0x1c) | 绘图 | 绘制图像 | 无 |
| 29 (0x1d) | 调色板设定 | 设置调色板 | 无 |
| 30 (0x1e) | 武将重绘 | 重新绘制武将 | 无 |
| 31 (0x1f) | 地图视点切换 | 切换视点 | int_data[0-1]=坐标 |
| 32 (0x20) | 武将头像状态 | 设置头像 | int_data[0]=人物 |
| 33 (0x21) | 战场物体添加 | 添加物体 | 无 |
| 34 (0x22) | 动画 | 播放动画 | text=动画文件名 |
| 35 (0x23) | 音效 | 播放音效 | text=音效文件名 |
| 36 (0x24) | CD音轨 | 播放CD音乐 | text=音轨 |
| 37 (0x25) | 武将进入地点测试 | 位置测试 | 无 |
| 38 (0x26) | 武将进入区域测试 | 区域测试 | 无 |
| 39 (0x27) | 背景显示 | 显示背景 | 无 |
| 40 (0x28) | 自由R启动指令 | R启动 | 无 |
| 41 (0x29) | 地图头像显示 | 头像显示 | 无 |
| 42 (0x2a) | 地图头像移动 | 头像移动 | 无 |
| 43 (0x2b) | 地图头像消失 | 头像消失 | 无 |
| 44 (0x2c) | 地图文字显示 | 文字显示 | text |
| 45 (0x2d) | 武将点击测试 | 点击测试 | 无 |
| 46 (0x2e) | 武将相邻测试 | 相邻测试 | 无 |
| 47 (0x2f) | 清除人物 | 清除人物 | 无 |
| 48 (0x30) | 武将出现 | 武将登场 | int_data[0]=人物, [1]=X, [2]=Y, [3]=方向, [4]=动作 |
| 49 (0x31) | 武将消失 | 武将消失 | int_data[0]=人物 |
| 50 (0x32) | 武将移动 | 移动武将 | int_data[0]=人物, [1]=X, [2]=Y |
| 51 (0x33) | 武将转向 | 改变朝向 | int_data[0]=人物, [1]=方向(0北1东2南3西) |
| 52 (0x34) | 武将动作 | 做动作 | int_data[0]=人物, [1]=动作ID |
| 53 (0x35) | 武将形象改变 | 更换形象 | int_data[0]=人物, [1]=形象 |
| 54 (0x36) | 武将状态测试 | 状态测试 | 无 |
| 55 (0x37) | 钱/章节/忠奸测试 | 数值测试 | 无 |
| 56 (0x38) | 武将能力设定 | 设定能力 | int_data[0]=人物 |
| 57 (0x39) | 武将等级提升 | 提升等级 | int_data[0]=人物, [1]=等级 |
| 58 (0x3a) | 钱/章节/忠奸设置 | 设置数值 | 无 |
| 59 (0x3b) | 武将加入 | 加入队伍 | int_data[0]=人物 |
| 60 (0x3c) | 武将加入测试 | 加入测试 | 无 |
| 61 (0x3d) | 获得物品 | 获得物品 | int_data[0]=物品索引 |
| 62 (0x3e) | 加入装备设定 | 装备设定 | 无 |
| 63 (0x3f) | 回合测试 | 回合条件 | 无 |
| 64 (0x40) | 行动方测试 | 行动方 | 无 |
| 65 (0x41) | 战场人数测试 | 人数条件 | 无 |
| 66 (0x42) | 战斗胜利测试 | 胜利条件 | 无 |
| 67 (0x43) | 战斗失败测试 | 失败条件 | 无 |
| 68 (0x44) | 战斗初始化 | 初始化战斗 | int_data[0]=地图ID |
| 69 (0x45) | 战场全局变量 | 战场变量 | 无 |
| 70 (0x46) | 友军出场设定 | 友军设定 | 无 |
| 71 (0x47) | 敌军出场设定 | 敌军设定 | int_data含人物/坐标/方向/等级等 |
| 72 (0x48) | 敌方装备设定 | 敌方装备 | 无 |
| 73 (0x49) | 战斗结束 | 结束战斗 | 无 |
| 74 (0x4a) | 我军出场强制设定 | 强制出场 | 无 |
| 75 (0x4b) | 我军出场设定 | 出场设定 | 无 |
| 76 (0x4c) | 隐藏武将出现 | 隐藏出现 | 无 |
| 77 (0x4d) | 武将状态变更 | 状态变更 | 无 |
| 78 (0x4e) | 武将方针变更 | 方针变更 | 无 |
| 79 (0x4f) | 战场转向设置 | 转向设置 | 无 |
| 80 (0x50) | 战场动作设定 | 动作设定 | 无 |
| 81 (0x51) | 战场恢复行动权 | 恢复行动 | 无 |
| 82 (0x52) | 兵种改变 | 改变兵种 | 无 |
| 83 (0x53) | 战场撤退 | 撤退 | 无 |
| 84 (0x54) | 战场撤退确认 | 撤退确认 | 无 |
| 85 (0x55) | 战场复活 | 复活武将 | 无 |
| 86 (0x56) | 天气类别设定 | 天气设定 | 无 |
| 87 (0x57) | 当前天气设定 | 当前天气 | 无 |
| 88 (0x58) | 战场障碍设定 | 障碍设定 | 无 |
| 89 (0x59) | 战利品 | 战利品设定 | 无 |
| 90 (0x5a) | 战场操作开始 | 操作开始 | 无 |
| 91 (0x5b) | 战场高亮区域 | 高亮区域 | 无 |
| 92 (0x5c) | 战场高亮人物 | 高亮人物 | 无 |
| 93 (0x5d) | 回合上限设定 | 回合上限 | 无 |
| 94 (0x5e) | 武将不同测试 | 不同测试 | 无 |
| 95 (0x5f) | 单挑结束 | 单挑结束 | 无 |
| 96 (0x60) | 单挑武将出场 | 单挑出场 | 无 |
| 97 (0x61) | 单挑胜负显示 | 胜负显示 | 无 |
| 98 (0x62) | 单挑阵亡 | 单挑阵亡 | 无 |
| 99 (0x63) | 单挑对话 | 单挑对话 | text |
| 100 (0x64) | 单挑动作 | 单挑动作 | 无 |
| 101 (0x65) | 单挑攻击1 | 单挑攻击 | 无 |
| 102 (0x66) | 单挑攻击2 | 单挑攻击2 | 无 |
| 103 (0x67) | 章名 | 设置章名 | text |
| 104 (0x68) | 单挑开始 | 开始单挑 | 无 |
| 105 (0x69) | 旁白 | 旁白文字 | text |
| 106 (0x6a) | Game Over | 游戏结束 | 无 |
| 107 (0x6b) | 法术 | 释放法术 | 无 |
| 108 (0x6c) | 武将能力复制 | 复制能力 | 无 |
| 109 (0x6d) | 相对复活或移动 | 相对操作 | 无 |
| 110 (0x6e) | 概率测试 | 概率判断 | 无 |
| 111 (0x6f) | 丢弃物品 | 丢弃物品 | 无 |
| 112 (0x70) | 能力选择复制 | 选择复制 | 无 |
| 113 (0x71) | 特效请求 | 显示特效 | 无 |
| 114 (0x72) | 信息传送 | 传送信息 | 无 |
| 115 (0x73) | 人物五围和测试 | 五围测试 | 无 |
| 116 (0x74) | 开/禁存档 | 存档控制 | int_data[0]=0禁/1开 |
| 117 (0x75) | S特殊形象指定 | 特殊形象 | 无 |
| 118 (0x76) | 无条件跳转 | 跳转 | int_data[0]=目标ord |
| 119 (0x77) | 变量运算 | 变量计算 | int_data[0]=变量, [1]=运算符, [4]=值 |
| 120 (0x78) | 整型变量赋值 | 整型赋值 | 无 |
| 121 (0x79) | 变量测试(扩展) | 扩展条件 | 无 |
| 122 (0x7A) | 对话3 | 对话类型3 | text |
| 123 (0x7B) | 信息传送测试 | 传送测试 | 无 |

> **注意**: 完整指令参数定义请参考 `Instructions.json` 文件。

---

## 🏗️ 曹操传剧本结构

```
Root
├── Scene (id=-1)          # 场景
│   └── Section (id=-2)    # 章节
│       ├── 指令 (id=0-123) # 具体指令
│       │   └── children    # 子指令(嵌套结构)
│       └── 指令...
└── Scene...
```

**关键字段**:
- `id`: 指令类型ID (-1=Scene, -2=Section, 0-123=具体指令)
- `ord`: 指令序号（全局唯一，用于定位和引用）
- `int_data`: 整数参数数组（最多20个元素）
- `text`: 文本参数（UTF-8，可选）
- `children`: 子指令数组（可选，形成树状嵌套）

---

## 💻 Python 使用示例

### 基础用法

```python
import requests

BASE_URL = "http://localhost:8080"

def add_instruction(after_ord, instruction_id, int_data=None, text=None):
    """添加指令"""
    payload = {
        "afterOrd": after_ord,
        "instructionId": instruction_id,
        "intData": int_data or [],
        "text": text or ""
    }
    response = requests.post(f"{BASE_URL}/api/add_instruction", json=payload)
    return response.json()

def find_index(list_type, name):
    """查找下拉框索引"""
    payload = {"listType": list_type, "name": name}
    response = requests.post(f"{BASE_URL}/api/find_combo_index", json=payload)
    return response.json()["index"]
```

### 实战示例

```python
# 1. 添加注释
add_instruction(-1, 2, text="这是一条注释")

# 2. 添加延时
add_instruction(-1, 9, int_data=[100])  # 延时100ms

# 3. 添加对话（需要先查找人物ID）
person_id = find_index("person", "曹操")
add_instruction(-1, 20, int_data=[person_id], text="&曹操\n你好！")

# 4. 添加武将出现
person_id = find_index("person", "夏侯惇")
dir_id = find_index("direction", "朝东")
add_instruction(-1, 48, int_data=[person_id, 10, 20, dir_id, 0])

# 5. 批量添加（推荐）
payload = {
    "afterOrd": -1,
    "instructions": [
        {"id": 2, "text": "=== 开场剧情 ==="},
        {"id": 48, "intData": [0, 10, 20, 0, 0]},
        {"id": 20, "intData": [0], "text": "&曹操\n诸位将士，今日攻取汉中！"},
        {"id": 5, "intData": [1900, -1, -1], "children": [
            {"id": 20, "intData": [1], "text": "&夏侯惇\n末将愿为先锋！"},
            {"id": 0, "intData": [0]}
        ]}
    ]
}
response = requests.post(f"{BASE_URL}/api/add_instructions_batch", json=payload)
```

---

## ⚠️ 注意事项

1. **编码**: 所有文本参数必须使用 UTF-8 编码
2. **ord值**: `afterOrd` 必须是已存在的指令的 ord 值，使用 -1 添加到末尾
3. **intData长度**: 最多20个元素，超出会被截断
4. **线程安全**: HTTP服务器在后台线程运行，操作时避免同时在编辑器中进行手动操作
5. **CORS**: 服务器已启用 CORS，支持跨域请求
6. **数据加载**: **重要！** 人物(`person`)、物品(`item`)、兵种(`job`)、法术(`magic`)、特技(`skill`)等列表需要在编辑器中**打开剧本文件后**才会加载。方向(`direction`)、天气(`weather`)、阵营(`camp`)、动作(`gesture`)等硬编码列表始终可用。
7. **MCP vs REST**: MCP端点(`/mcp`)和REST端点(`/api/*`)共存互不影响，功能等价

---

## 🔧 故障排除

### 服务器无法启动
- 检查端口8080是否被占用
- 尝试修改环境变量 `CCZ_EDITOR_PORT` 使用其他端口

### MCP连接失败
- 确认编辑器已启动HTTP服务器（菜单中点击启动）
- 确认 `mcp-bridge.js` 路径正确
- 检查 `CCZ_EDITOR_PORT` 环境变量与编辑器端口一致
- 在终端测试: `curl -X POST http://localhost:8080/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'`

### 请求返回400错误
- 检查JSON格式是否正确
- 确认 instructionId 在 0-123 范围内（或-1/-2）
- 确认 afterOrd 对应的指令存在

### 中文乱码
- 确保发送的请求使用 UTF-8 编码
- Python requests 库默认使用 UTF-8

---

## 📞 技术支持

- 示例脚本: `test_http_api.py`
- 指令元数据: `cczEditor2/Instructions.json`
- MCP桥接脚本: `mcp-bridge.js`
- GitHub: https://github.com/Puluomiyuhun/ccz-SceneEditor
