"""
cczEditor2 HTTP API 测试脚本
使用前请先在编辑器中启动 HTTP 服务器（文件菜单 -> 启动HTTP服务器）
"""

import requests
import json

BASE_URL = "http://localhost:8080"

def test_status():
    """测试服务器状态"""
    print("=== 测试服务器状态 ===")
    try:
        response = requests.get(f"{BASE_URL}/api/status")
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def find_combo_index(list_type, name):
    """查找下拉框选项的索引"""
    url = f"{BASE_URL}/api/find_combo_index"
    payload = {
        "listType": list_type,
        "name": name
    }
    response = requests.post(url, json=payload)
    result = response.json()
    if result["found"]:
        print(f"找到 '{name}' 在 '{list_type}' 中的索引: {result['index']}")
        return result["index"]
    else:
        print(f"未找到 '{name}' 在 '{list_type}' 中")
        return -1

def add_instruction(after_ord, instruction_id, int_data=None, text=None):
    """添加指令"""
    url = f"{BASE_URL}/api/add_instruction"
    payload = {
        "afterOrd": after_ord,
        "instructionId": instruction_id,
        "intData": int_data or [],
        "text": text or ""
    }
    print(f"\n发送请求: {json.dumps(payload, ensure_ascii=False)}")
    response = requests.post(url, json=payload)
    result = response.json()
    print(f"响应: {json.dumps(result, ensure_ascii=False)}")
    return result["success"]

def find_name():
    names = ['曹操','夏侯惇','郭嘉','典韦','许褚','荀彧','曹仁','张辽','徐晃','曹洪',
            '夏侯渊','荀攸','程昱','贾诩','司马懿','关羽','刘备','张飞','诸葛亮','赵云',
            '吕布','貂蝉','袁绍','孙策','周瑜','黄盖','曹绫','姜维','柳隐','句扶','杨兰','费曜']
    for name in names:
        r = requests.post(f'{BASE_URL}/api/find_combo_index', json={'listType':'person','name':name})
        d = r.json()
        if d['found']:
            print(name)

def main():
    print("=" * 50)
    print("cczEditor2 HTTP API 测试")
    print("=" * 50)
    
    # 1. 测试服务器状态
    if not test_status():
        print("\n服务器未运行！请先在编辑器中启动 HTTP 服务器。")
        print("菜单: 文件 -> 启动HTTP服务器")
        return
    
    # 2. 测试查找人物ID
    print("\n=== 测试查找功能 ===")
    person_idx = find_combo_index("person", "姜维")
    direction_idx = find_combo_index("direction", "朝东")
    
    # 3. 测试添加简单指令（内部信息，id=2）
    print("\n=== 测试添加指令 ===")
    success = add_instruction(
        after_ord=-1,  # 添加到根节点末尾
        instruction_id=2,  # 内部信息
        text="这是一条测试信息"
    )
    print(f"添加内部信息指令: {'成功' if success else '失败'}")
    
    # 4. 测试添加延时指令（id=9）
    success = add_instruction(
        after_ord=-1,
        instruction_id=9,  # 延时
        int_data=[100]  # 延时100ms
    )
    print(f"添加延时指令: {'成功' if success else '失败'}")
    
    # 5. 测试添加人物对话指令（id=18）
    if person_idx >= 0:
        success = add_instruction(
            after_ord=-1,
            instruction_id=18,  # 选择框/对话
            int_data=[person_idx],
            text="你好，我是曹操"
        )
        print(f"添加对话指令: {'成功' if success else '失败'}")
    
    print("\n" + "=" * 50)
    print("测试完成！请检查编辑器中的指令树。")
    print("=" * 50)

if __name__ == "__main__":
    main()