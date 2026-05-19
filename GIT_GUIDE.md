# Git 协作指南 - 计算机图形学实验团队

> 本文档面向团队成员，说明如何正确拉取代码、创建分支、提交更改和发起 Pull Request。

---

## 📋 前置准备

### 1. 安装 Git

**Windows 用户:**
```bash
# 下载并安装 Git for Windows
# https://git-scm.com/download/win

# 验证安装
git --version
```

**配置用户信息（只需一次）:**
```bash
git config --global user.name "你的姓名"
git config --global user.email "你的邮箱@example.com"
```

### 2. 安装 CMake 和编译器

确保已安装：
- CMake 3.15+
- MinGW-w64 或 Visual Studio

---

## 🚀 快速开始（第一次使用）

### 步骤 1: 克隆仓库

```bash
# 在项目目录的父目录执行
git clone <仓库地址> graphics_2
cd graphics_2
```

> 💡 **注意**: 如果是本地仓库，直接 `cd` 到项目目录即可。

### 步骤 2: 验证项目结构

```bash
# 查看当前分支（应该是 master）
git branch

# 查看文件状态
git status
```

### 步骤 3: 编译运行

```bash
# 创建构建目录
cmake -B build -S . -G "MinGW Makefiles"

# 编译
cd build
mingw32-make

# 运行
./GraphicsLab_ShadowMapping.exe
```

---

## 🌿 分支工作流程

### 分支说明

```
master (主分支，受保护)
├── feature-model      ← 成员 B 的模型分支
├── feature-material   ← 成员 C 的材质分支
├── feature-camera     ← 成员 D 的相机分支
└── feature-gui        ← 成员 E 的 GUI 分支
```

> 🔴 **重要**: 永远不要直接向 `master` 分支推送代码！

---

## 📝 完整工作流程示例

### 场景：成员 B 要实现模型加载功能

#### 第 1 步：切换到主分支并更新

```bash
# 确保在 master 分支
git checkout master

# 拉取最新代码（如果有远程仓库）
git pull origin master
```

#### 第 2 步：创建功能分支

```bash
# 基于 master 创建新分支
git checkout -b feature-model

# 验证分支切换成功
git branch
# 输出: * feature-model
#       master
```

#### 第 3 步：编写代码

打开 `main.cpp`，找到对应的代码插槽：

```cpp
// ============================================================================
// [插槽 1] 成员 B - 模型渲染系统
// ============================================================================
// TODO: 在此渲染所有场景模型
// ...
```

实现你的代码，替换 `TODO` 部分。

#### 第 4 步：测试代码

```bash
cd build
mingw32-make
./GraphicsLab_ShadowMapping.exe
```

确保能正常编译运行。

#### 第 5 步：提交更改

```bash
# 查看修改了哪些文件
git status

# 添加修改的文件
git add main.cpp

# 也可以添加所有修改
git add .

# 提交更改（写有意义的提交信息）
git commit -m "feat(model): 实现 OBJ 模型加载功能

- 添加 Model 类封装模型数据
- 实现 OBJ 文件解析
- 加载 5 个场景模型（建筑、箱子、树木等）
- 在 RenderScene 中集成模型渲染

Closes #1"
```

> 💡 **提交信息规范**: `type(scope): description`
> - `type`: feat(新功能), fix(修复), docs(文档), refactor(重构)
> - `scope`: 模块名（model, material, camera, gui）
> - `description`: 简短描述

#### 第 6 步：推送到远程

```bash
# 第一次推送需要设置上游分支
git push -u origin feature-model

# 后续推送只需
git push
```

#### 第 7 步：创建 Pull Request

1. 打开 GitHub/GitLab 仓库页面
2. 点击 "New Pull Request" 或 "Merge Request"
3. 选择 `feature-model` → `master`
4. 填写 PR 标题和描述：

```markdown
## [成员B] 实现模型加载系统

### 完成内容
- [x] 实现 OBJ 文件格式解析
- [x] 创建 Model 类封装
- [x] 加载 5 个独立模型
- [x] 集成到 RenderScene 插槽

### 测试方法
1. 编译项目
2. 运行程序
3. 确认场景中有 5 个模型正确显示

### 截图
[附上运行截图]
```

5. 提交 PR，等待组长审核

---

## 🔄 日常开发流程

### 每天开始工作前

```bash
# 1. 切换到 master 并更新
git checkout master
git pull origin master

# 2. 切换到你的功能分支
git checkout feature-model

# 3. 合并 master 的最新更改（避免冲突）
git merge master
```

### 开发过程中

```bash
# 查看当前修改
git status

# 查看具体修改内容
git diff

# 添加并提交
git add <文件名>
git commit -m "feat: 描述你的修改"

# 推送到远程
git push
```

### 遇到冲突时

如果在 `git merge master` 或 `git pull` 时遇到冲突：

```bash
# 1. 查看冲突文件
git status

# 2. 打开冲突文件，找到冲突标记
<<<<<<< HEAD
你的代码
=======
别人的代码
>>>>>>> master

# 3. 手动编辑，保留需要的代码，删除冲突标记

# 4. 标记冲突已解决
git add <冲突文件>

# 5. 完成合并
git commit -m "merge: 解决与 master 的冲突"
```

---

## 🆘 常见问题

### Q1: 忘记创建分支，直接在 master 上修改了

```bash
# 保存当前修改
git stash

# 创建并切换到新分支
git checkout -b feature-xxx

# 恢复修改
git stash pop

# 提交到新分支
git add .
git commit -m "feat: ..."
```

### Q2: 提交信息写错了

```bash
# 修改最后一次提交信息
git commit --amend -m "新的提交信息"

# 如果已推送到远程，需要强制推送（谨慎使用）
git push --force-with-lease
```

### Q3: 想撤销最后一次提交

```bash
# 保留修改，只撤销提交
git reset --soft HEAD~1

# 完全撤销（修改也删除，谨慎！）
git reset --hard HEAD~1
```

### Q4: 如何查看提交历史

```bash
# 简洁查看
git log --oneline

# 图形化查看分支
git log --oneline --graph --all
```

### Q5: 如何查看某个文件的修改历史

```bash
git log -p main.cpp
```

---

## 📁 项目文件说明

```
graphics_2/
├── .git/                   # Git 仓库数据（不要动）
├── .gitignore              # 忽略规则
├── CMakeLists.txt          # 构建配置
├── README.md               # 项目说明
├── GIT_GUIDE.md            # 本文件
├── main.cpp                # 主程序（在此修改）
├── ShadowPipeline.hpp      # 阴影管线（组长维护）
├── glm/                    # GLM 数学库
├── include/                # 头文件
├── lib/                    # 库文件
├── src/                    # 源文件
└── build/                  # 构建输出（被忽略）
```

> ⚠️ **不要修改的文件**: `ShadowPipeline.hpp`（除非与组长沟通）

---

## ✅ 提交前检查清单

在提交代码前，请确认：

- [ ] 代码能正常编译
- [ ] 程序能正常运行
- [ ] 修改了正确的代码插槽
- [ ] 提交信息符合规范
- [ ] 没有提交不必要的文件（如 build/ 目录）
- [ ] 已测试基本功能

---

## 📞 求助渠道

遇到问题时的解决顺序：

1. 查看本指南的 "常见问题" 部分
2. 搜索错误信息（百度/Google）
3. 询问组长
4. 在团队群聊中提问

---

## 📚 学习资源

- [Git 官方文档](https://git-scm.com/doc)
- [Git 简明指南](https://rogerdudler.github.io/git-guide/index-zh.html)
- [Learn Git Branching](https://learngitbranching.js.org/?locale=zh_CN)（交互式学习）

---

> **最后更新**: 2026年5月19日  
> **文档维护**: 组长
