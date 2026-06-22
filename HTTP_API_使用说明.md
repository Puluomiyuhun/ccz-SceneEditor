# cczEditor2 HTTP API 使用说明

## 📖 概述

cczEditor2（曹操传MOD剧本编辑器）内置了 HTTP 服务器，允许外部程序（如 AI Agent）通过 RESTful API 远程操作剧本编辑器。

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

## 📡 API 端点

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

**失败响应**:
```json
{
  "success": false,
  "message": "找不到指定ord的指令"
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

**参数说明**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| listType | string | 是 | 列表类型，见下方支持的类型 |
| name | string | 是 | 要查找的名称（UTF-8编码） |

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

**响应**:
```json
{
  "index": 0,
  "found": true
}
```

**未找到响应**:
```json
{
  "index": -1,
  "found": false
}
```

---

### 4. POST /api/add_instructions_batch - 批量添加指令

一次性添加多个指令，支持嵌套层级关系（children）。适合 AI Agent 生成完整的事件片段。

**请求体 (JSON)**:
```json
{
  "afterOrd": -1,
  "instructions": [
    {
      "id": 2,
      "intData": [0],
      "text": "这是一条注释"
    },
    {
      "id": 9,
      "intData": [100]
    },
    {
      "id": 5,
      "intData": [1900, -1, -1],
      "children": [
        {
          "id": 20,
          "intData": [0],
          "text": "&曹操\n你好！"
        },
        {
          "id": 0,
          "intData": [0]
        }
      ]
    }
  ]
}
```

**参数说明**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| afterOrd | int | 是 | 在ord为多少的指令后面添加，-1表示添加到根节点末尾 |
| instructions | array | 是 | 指令数组，每个元素是一个指令对象 |

**指令对象格式**:

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| id | int | 是 | 指令类型ID (0-123) |
| intData | int[] | 否 | 整数参数数组，最多20个元素 |
| text | string | 否 | 文本参数（UTF-8编码） |
| children | array | 否 | 子指令数组，格式与instructions相同（递归嵌套） |

**响应**:
```json
{
  "success": true,
  "message": "批量添加成功",
  "count": 5
}
```

**失败响应**:
```json
{
  "success": false,
  "message": "批量添加失败，请检查JSON格式和指令ID",
  "count": 0
}
```

**使用示例**:

```python
# 批量添加一个完整的对话事件
payload = {
    "afterOrd": -1,
    "instructions": [
        {"id": 2, "text": "=== 开场剧情 ==="},
        {"id": 9, "intData": [50]},
        {"id": 48, "intData": [0, 10, 20, 0, 0]},  # 曹操出现
        {"id": 20, "intData": [0], "text": "&曹操\n诸位将士，今日我们要攻取汉中！"},
        {"id": 9, "intData": [100]},
        {"id": 5, "intData": [1900, -1, -1], "children": [  # 变量测试
            {"id": 20, "intData": [1], "text": "&夏侯惇\n末将愿为先锋！"},
            {"id": 0, "intData": [0]}  # 事件结束
        ]},
        {"id": 0, "intData": [0]}  # 事件结束
    ]
}
response = requests.post("http://localhost:8080/api/add_instructions_batch", json=payload)
print(response.json())
```

---

## 📋 指令列表 (instructionId)

以下是所有124种指令的ID和名称：

| ID | 名称 | 说明 | 主要参数 |
|----|------|------|----------|
| 0 | 事件结束 | 结束当前事件 | 无 |
| 1 | 子事件设定 | 设定子事件 | 无 |
| 2 | 内部信息 | 注释/内部信息 | text |
| 3 | 单挑 | 武将单挑 | int_data[0]=武将1, int_data[1]=武将2 |
| 4 | 习得策略 | 人物习得策略 | int_data[0]=人物ID |
| 5 | 变量测试 | 条件判断 | int_data[0-49]=变量列表(两组) |
| 6 | 习得必杀 | 人物习得必杀 | int_data[0]=人物, int_data[1-10]=必杀列表 |
| 7 | 商店商家变更 | 变更商店 | int_data[0]=商家ID |
| 8 | 菜单处理 | 显示菜单 | 无 |
| 9 | 延时 | 等待指定时间 | int_data[0]=毫秒数 |
| 10 | 初始化局部变量 | 初始化变量 | 无 |
| 11 | 变量赋值 | 给变量赋值 | int_data[0]=变量ID, int_data[1]=值 |
| 12 | 选择框 | 显示选项 | text=选项文本(用\n分隔) |
| 13 | 结束Scene | 结束场景 | 无 |
| 14 | 对话 | 显示对话 | text=对话内容(&人名\n对话) |
| 15 | 战场形象替换 | 替换形象 | int_data[0]=原形象, int_data[1]=新形象 |
| 16 | 更换R形象 | 更换R图 | int_data[0]=人物, int_data[1]=形象ID |
| 17 | 场所名 | 设置场所名 | text=场所名 |
| 18 | 事件名称设定 | 设置事件名 | text=事件名 |
| 19 | case | 选择分支 | int_data[0]=case值 |
| 20 | 对话 | 对话(另一种) | text=对话内容 |
| 21 | 旁白 | 旁白文字 | text=旁白内容 |
| 22 | 获得物品 | 获得物品 | int_data[0]=人物, int_data[1]=物品 |
| 23 | 失去物品 | 失去物品 | int_data[0]=人物, int_data[1]=物品 |
| 24 | CD音轨 | 播放CD音乐 | int_data[0]=音轨号 |
| 25 | R数字 | 显示数字 | int_data[0]=数值 |
| 26 | 产生随机数 | 生成随机数 | int_data[0]=最小值, int_data[1]=最大值 |
| 27 | 背景显示 | 显示背景 | int_data[0]=背景ID |
| 28 | 绘图 | 绘制图像 | 无 |
| 29 | 调色板设定 | 设置调色板 | 无 |
| 30 | 武将重绘 | 重新绘制武将 | 无 |
| 31 | 武将消失 | 武将消失 | int_data[0]=武将ID |
| 32 | 武将移动 | 移动武将 | int_data[0]=武将, int_data[1-2]=目标坐标 |
| 33 | 武将转向 | 武将改变朝向 | int_data[0]=武将, int_data[1]=方向 |
| 34 | 武将动作 | 武将做动作 | int_data[0]=武将, int_data[1]=动作ID |
| 35 | 镜头移动 | 移动镜头 | int_data[0-1]=目标坐标 |
| 36 | 镜头缩放 | 缩放镜头 | int_data[0]=缩放比例 |
| 37 | 战场明暗变化 | 改变亮度 | int_data[0]=亮度值 |
| 38 | 控制动态图 | 控制动图 | int_data[0]=动图ID |
| 39 | 黑幕设置 | 设置黑幕 | int_data[0-3]=矩形区域 |
| 40 | 部队出现 | 部队出现 | int_data[0]=部队ID, int_data[1-2]=坐标 |
| 41 | 部队消失 | 部队消失 | int_data[0]=部队ID |
| 42 | 部队移动 | 部队移动 | int_data[0]=部队, int_data[1-2]=目标 |
| 43 | 修改属性 | 修改人物属性 | int_data[0]=人物, int_data[1]=属性类型 |
| 44 | 加入队伍 | 人物加入 | int_data[0]=人物ID |
| 45 | 离开队伍 | 人物离队 | int_data[0]=人物ID |
| 46 | 战斗开始 | 开始战斗 | 无 |
| 47 | 清除人物 | 清除人物数据 | 无 |
| 48 | 武将出现 | 武将登场 | int_data[0]=武将, int_data[1-2]=坐标, int_data[3]=方向 |
| 49 | 武将消失(扩展) | 武将消失带效果 | int_data[0]=武将, int_data[6]=效果 |
| 50 | 武将移动(扩展) | 武将移动带路径 | int_data[0]=武将, int_data[1-4]=路径点 |
| 51 | 武将转向(扩展) | 武将转向 | int_data[0]=武将, int_data[1]=方向 |
| 52 | 武将动作(扩展) | 武将动作 | int_data[0]=武将, int_data[1]=动作 |
| ... | ... | ... | ... |
| 118 | 无条件跳转 | 跳转到指定ord | int_data[0]=目标ord |
| 119 | 变量运算 | 变量计算 | int_data[0]=运算类型, int_data[1]=变量, int_data[4]=值 |
| 121 | 变量测试(扩展) | 扩展条件判断 | int_data[0]=条件类型, int_data[1]=变量, int_data[4]=比较值 |

> **注意**: 完整指令列表请参考编辑器中的指令树或 `Instructions.json` 文件。

---

## 🏗️ 曹操传剧本格式

### JSON导出格式示例

```json
{
  "version": "1.0",
  "filename": "R_05.EEX",
  "instructions": [
    {
      "id": -1,
      "name": "Scene",
      "ord": 0,
      "int_data": [0, 0, 0, 0, 0],
      "children": [
        {
          "id": -2,
          "name": "Section",
          "ord": 1,
          "int_data": [0, 0, 0, 0, 0],
          "children": [
            {
              "id": 2,
              "name": "2:内部信息",
              "ord": 2,
              "int_data": [0, 0, 0, 0, 0]
            },
            {
              "id": 20,
              "name": "14:对话",
              "ord": 37,
              "int_data": [0, 0, 0, 0, 0],
              "text": "&曹绫\n你怎么不说话？"
            },
            {
              "id": 5,
              "name": "5:变量测试",
              "ord": 21,
              "int_data": [1900, -1, -1, -1, -1, ...],
              "children": [
                {
                  "id": 11,
                  "name": "b:变量赋值",
                  "ord": 22,
                  "int_data": [3500, 1, 0, 0, 0]
                }
              ]
            }
          ]
        }
      ]
    }
  ]
}
```

### 结构说明

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
- `ord`: 指令序号（全局唯一，用于定位）
- `int_data`: 整数参数数组（20个元素）
- `text`: 文本参数（可选）
- `children`: 子指令数组（可选，形成树状结构）

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
# 1. 添加内部信息
add_instruction(-1, 2, text="这是一条注释")

# 2. 添加延时
add_instruction(-1, 9, int_data=[100])  # 延时100ms

# 3. 添加对话（需要先查找人物ID）
person_id = find_index("person", "曹操")
add_instruction(-1, 20, int_data=[person_id], text="&曹操\n你好！")

# 4. 添加选择框
add_instruction(-1, 12, text="选项1\n选项2\n选项3")

# 5. 添加变量赋值
add_instruction(-1, 11, int_data=[1900, 1])  # 变量1900赋值为1

# 6. 添加武将出现
person_id = find_index("person", "夏侯惇")
dir_id = find_index("direction", "朝东")
add_instruction(-1, 48, int_data=[person_id, 10, 20, dir_id, 0])
```

### AI Agent Skill 封装

```python
class CczEditorSkill:
    """曹操传剧本编辑器 Skill"""
    
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
    
    def _post(self, endpoint, data):
        response = requests.post(f"{self.base_url}{endpoint}", json=data)
        return response.json()
    
    def add_comment(self, text, after_ord=-1):
        """添加注释"""
        return self._post("/api/add_instruction", {
            "afterOrd": after_ord,
            "instructionId": 2,
            "text": text
        })
    
    def add_delay(self, ms, after_ord=-1):
        """添加延时"""
        return self._post("/api/add_instruction", {
            "afterOrd": after_ord,
            "instructionId": 9,
            "intData": [ms]
        })
    
    def add_dialog(self, person_name, text, after_ord=-1):
        """添加对话"""
        person_id = self.find_index("person", person_name)
        return self._post("/api/add_instruction", {
            "afterOrd": after_ord,
            "instructionId": 20,
            "intData": [person_id],
            "text": f"&{person_name}\n{text}"
        })
    
    def add_choice(self, options, after_ord=-1):
        """添加选择框"""
        return self._post("/api/add_instruction", {
            "afterOrd": after_ord,
            "instructionId": 12,
            "text": "\n".join(options)
        })
    
    def add_variable_set(self, var_id, value, after_ord=-1):
        """设置变量"""
        return self._post("/api/add_instruction", {
            "afterOrd": after_ord,
            "instructionId": 11,
            "intData": [var_id, value]
        })
    
    def add_character_appear(self, name, x, y, direction="默认方向", after_ord=-1):
        """武将出现"""
        person_id = self.find_index("person", name)
        dir_id = self.find_index("direction", direction)
        return self._post("/api/add_instruction", {
            "afterOrd": after_ord,
            "instructionId": 48,
            "intData": [person_id, x, y, dir_id, 0]
        })
    
    def add_batch(self, instructions, after_ord=-1):
        """批量添加指令（保留层级关系）
        
        Args:
            instructions: 指令列表，每个指令是dict，可包含id, intData, text, children
            after_ord: 插入位置
        
        Returns:
            dict: {"success": bool, "count": int, "message": str}
        """
        return self._post("/api/add_instructions_batch", {
            "afterOrd": after_ord,
            "instructions": instructions
        })
    
    def find_index(self, list_type, name):
        """查找索引"""
        result = self._post("/api/find_combo_index", {
            "listType": list_type,
            "name": name
        })
        return result["index"]
```

---

## ⚠️ 注意事项

1. **编码**: 所有文本参数必须使用 UTF-8 编码
2. **ord值**: `afterOrd` 必须是已存在的指令的 ord 值，使用 -1 添加到末尾
3. **intData长度**: 最多20个元素，超出会被截断
4. **线程安全**: HTTP服务器在后台线程运行，不会影响编辑器主界面
5. **CORS**: 服务器已启用 CORS，支持跨域请求
6. **数据加载**: **重要！** 人物(`person`)、物品(`item`)、兵种(`job`)、法术(`magic`)、特技(`skill`)等列表需要在编辑器中**打开剧本文件后**才会加载。如果查询返回 `found: false`，请先在编辑器中打开一个剧本文件。方向(`direction`)、天气(`weather`)、阵营(`camp`)、动作(`gesture`)等硬编码列表始终可用。

---

## 🔧 故障排除

### 服务器无法启动
- 检查端口8080是否被占用
- 尝试使用其他端口（修改代码中的默认端口）

### 请求返回400错误
- 检查JSON格式是否正确
- 确认 instructionId 在 0-123 范围内
- 确认 afterOrd 对应的指令存在

### 中文乱码
- 确保发送的请求使用 UTF-8 编码
- Python requests 库默认使用 UTF-8

---

## 📞 技术支持

如有问题，请查看：
- 示例脚本: `F:\cczEditor2\test_http_api.py`
- 示例剧本: `F:\cczEditor2\123.json`
