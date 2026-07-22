# ABE Engine 开发环境

本文说明本仓库的 Docker 安装、Compose 环境配置和常用启动命令。完整架构设计见
[global_room_server_framework_design.md](global_room_server_framework_design.md)，服务端源码目录说明见
[server/README.md](../server/README.md)。

项目常驻工程约束见 [AGENTS.md](../AGENTS.md)。其中 `server/engine` 的所有项目公开接口必须能使用
C 或 C++11 编译，优先使用简洁、显式、便于人工维护的接口，禁止 C++14+ 和复杂高级语法，并尽量不使用 STL。

## 1. Docker 安装

推荐在 Ubuntu 22.04/24.04 上使用 Docker 官方 apt 仓库安装 Docker Engine、Buildx
和 Docker Compose 插件。这里使用的是 Compose v2 命令格式：`docker compose`。

```bash
sudo apt-get remove -y docker.io docker-doc docker-compose docker-compose-v2 podman-docker containerd runc

sudo apt-get update
sudo apt-get install -y ca-certificates curl

sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

sudo tee /etc/apt/sources.list.d/docker.sources > /dev/null <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF

sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo systemctl enable --now docker
sudo docker run hello-world
docker compose version
```

如果希望普通用户直接运行 Docker 命令，可以把当前用户永久加入 `docker` 组：

```bash
deploy/docker/dev.sh access
```

这一步会写入系统用户组配置，重启后仍然有效，不需要每次开机重复执行。注销并重新登录一次后，
新终端会自动获得 `docker` 组权限。如果希望当前终端立刻生效，可以临时执行：

```bash
newgrp docker
docker run hello-world
```

如果执行 `docker ps`、`docker compose` 或自动化工具访问 Docker 时出现
`permission denied while trying to connect to the docker API at unix:///var/run/docker.sock`，
先检查 socket 属主和当前用户组：

```bash
ls -l /var/run/docker.sock
id
getent group docker
```

正常情况下 `/var/run/docker.sock` 属于 `root:docker` 且权限类似 `srw-rw----`。
若当前用户不在 `docker` 组，执行：

```bash
deploy/docker/dev.sh access
```

注销并重新登录一次后再验证：

```bash
docker ps
```

如果不想重登，可以在当前终端临时执行 `newgrp docker` 后再运行 `docker ps`。`newgrp` 只影响当前 shell，
不是开机后必须重复执行的步骤。

注意：`docker` 组拥有接近 root 的权限，只建议给可信的本机开发账号使用。

## 2. Compose 文件

Docker 开发环境文件位于 `deploy/docker/`：

| 文件 | 用途 |
| --- | --- |
| `Dockerfile` | 构建开发容器，安装编译工具、C/C++ 依赖、gRPC、Kafka/RabbitMQ/Redis/MySQL 客户端库，并从源码构建 `libjuice`。 |
| `docker-compose.yml` | 启动 `dev`、`mysql`、`redis`、`rabbitmq`、`zookeeper`、`kafka`。 |
| `.env.example` | 本地环境变量模板。首次启动前复制为 `.env`。 |

Compose 中依赖服务镜像默认使用可直接访问的镜像前缀，避免 Docker Hub 超时：

| 变量 | 默认镜像 |
| --- | --- |
| `MYSQL_IMAGE` | `m.daocloud.io/docker.io/mysql:8.0` |
| `REDIS_IMAGE` | `m.daocloud.io/docker.io/redis:7-alpine` |
| `RABBITMQ_IMAGE` | `m.daocloud.io/docker.io/rabbitmq:3-management` |
| `ZOOKEEPER_IMAGE` | `m.daocloud.io/docker.io/zookeeper:3.9` |
| `KAFKA_IMAGE` | `m.daocloud.io/docker.io/apache/kafka:3.7.0` |

如果已经配置 Docker daemon 的阿里云 mirror，可以在 `.env` 中把这些变量改回 `mysql:8.0`、
`redis:7-alpine` 等原始 Docker Hub 名称。

当前 compose 使用 `network_mode: host`。容器和宿主机共享网络命名空间，服务会直接占用宿主机端口，
因此 `ports` 映射不会生效，也不应该和 `network_mode: host` 同时配置。
由于 `dev` 服务还把源码目录做了 bind mount，`/workspace` 里的文件权限最终以宿主机文件系统为准。
仓库内普通文本文件默认保持 `0644`，需要执行的脚本再单独设为 `0755`。
如果要把带源码的容器镜像拷贝到其他机器，使用 `portable` 构建目标；该目标会在镜像内把普通文件归一为
`0644`、目录归一为 `0755`，已有执行位的脚本仍保留执行权限。

```bash
# 在仓库根目录执行
deploy/docker/dev.sh portable

# 在目标机器上执行
docker load -i abe-engine-portable.tar
docker run --rm -it --network host abe-engine-portable:latest bash
```

## 3. 本地环境配置

首次启动前复制环境变量模板：

```bash
cd deploy/docker
cp .env.example .env
```

默认账号和端口如下：

| 服务 | 地址 | 默认配置 |
| --- | --- | --- |
| MySQL | `127.0.0.1:3306` | database: `abe_engine`, user: `abe`, password: `abe123`, root password: `rootpass` |
| Redis | `127.0.0.1:6379` | 开启 AOF 持久化 |
| RabbitMQ | `127.0.0.1:5672` | user: `abe`, password: `abe123` |
| RabbitMQ Management | `127.0.0.1:15672` | 浏览器访问管理后台 |
| ZooKeeper | `127.0.0.1:2181` | 匿名登录，仅用于本地开发 |
| Kafka | `127.0.0.1:9092` | 单 broker，自动创建 topic |

`dev` 容器内会预设这些业务环境变量：

| 变量 | 默认值 |
| --- | --- |
| `ABE_MYSQL_HOST` | `127.0.0.1` |
| `ABE_MYSQL_PORT` | `3306` |
| `ABE_MYSQL_DATABASE` | `${MYSQL_DATABASE:-abe_engine}` |
| `ABE_MYSQL_USER` | `${MYSQL_USER:-abe}` |
| `ABE_MYSQL_PASSWORD` | `${MYSQL_PASSWORD:-abe123}` |
| `ABE_REDIS_HOST` | `127.0.0.1` |
| `ABE_REDIS_PORT` | `6379` |
| `ABE_RABBITMQ_HOST` | `127.0.0.1` |
| `ABE_RABBITMQ_PORT` | `5672` |
| `ABE_KAFKA_BROKERS` | `127.0.0.1:9092` |
| `ABE_GRPC_PORT` | `50051` |
| `ABE_LOG_LEVEL` | `info` |

host 网络下端口由服务进程直接监听。启动前请确认宿主机没有占用这些端口：

```bash
ss -ltnp | grep -E ':(3306|6379|5672|15672|2181|9092)\b' || true
```

如果需要改端口，需要同步修改 `deploy/docker/docker-compose.yml` 里的服务监听配置和 `dev`
容器中的 `ABE_*` 连接配置。

### 3.1 代理与 IP 修改

如果本机在代理网络后面，可以在 `deploy/docker/.env` 里取消注释代理变量，或在执行前导出环境变量。
把代理地址换成你自己的 IP 和端口：

```bash
export HTTP_PROXY=http://<proxy-ip>:<proxy-port>
export HTTPS_PROXY=http://<proxy-ip>:<proxy-port>
export NO_PROXY=localhost,127.0.0.1,::1
docker compose up -d --build
```

当前 compose 会把 `HTTP_PROXY`、`HTTPS_PROXY` 和 `NO_PROXY` 作为 `dev` 镜像构建参数，也会传入
`dev` 容器运行环境。镜像拉取由 Docker daemon 执行，如果 `docker pull` 或基础镜像拉取仍然失败，需要单独按
Docker 官方 daemon 代理文档配置。

如果后端服务不在本机，而是换成了别的 IP，就把 `dev` 容器里的连接地址一并改掉：

- `ABE_MYSQL_HOST`
- `ABE_REDIS_HOST`
- `ABE_RABBITMQ_HOST`
- `ABE_KAFKA_BROKERS`

### 3.2 阿里云镜像加速

如果 Docker Hub 拉取镜像超时，可以把 Docker daemon 的 `registry-mirrors` 切到阿里云镜像加速地址。
阿里云加速器地址是账号专属地址，需要在阿里云容器镜像服务控制台复制完整 URL。

```bash
deploy/docker/dev.sh mirror-aliyun "<从阿里云控制台复制的完整加速地址>"
deploy/docker/dev.sh mirror-show
```

也可以写入 `deploy/docker/.env`：

```bash
ALIYUN_MIRROR_URL=<从阿里云控制台复制的完整加速地址>
deploy/docker/dev.sh mirror-aliyun
```

该命令会备份 `/etc/docker/daemon.json`，更新 `registry-mirrors`，然后重启 Docker daemon。
如果当前用户不在 `docker` 组，脚本会提示输入 `sudo` 密码。

## 4. 启动与进入开发容器

Docker 目录中提供统一操作脚本，脚本可以从任意目录执行：

| 命令 | 用途 |
| --- | --- |
| `deploy/docker/dev.sh start` | 启动环境，等价于 `docker compose up -d`。 |
| `deploy/docker/dev.sh stop` | 停止并移除容器，保留数据卷。 |
| `deploy/docker/dev.sh restart` | 重启整套环境。 |
| `deploy/docker/dev.sh build` | 构建镜像。 |
| `deploy/docker/dev.sh rebuild` | 无缓存重新构建镜像并启动环境。 |
| `deploy/docker/dev.sh status` | 查看服务状态。 |
| `deploy/docker/dev.sh logs` | 跟踪日志，可追加服务名，例如 `logs mysql`。 |
| `deploy/docker/dev.sh enter` | 进入 `dev` 开发容器。 |
| `deploy/docker/dev.sh clean` | 停止环境并删除数据卷。 |
| `deploy/docker/dev.sh config` | 检查 compose 配置。 |
| `deploy/docker/dev.sh portable` | 构建可迁移镜像并导出 `abe-engine-portable.tar`。 |
| `deploy/docker/dev.sh access` | 将当前登录用户永久加入 `docker` 组。 |
| `deploy/docker/dev.sh mirror-aliyun` | 配置 Docker daemon 使用阿里云镜像加速。 |
| `deploy/docker/dev.sh mirror-show` | 查看当前 Docker 镜像加速地址。 |

常用命令：

```bash
deploy/docker/dev.sh start
deploy/docker/dev.sh status
deploy/docker/dev.sh enter
deploy/docker/dev.sh rebuild
deploy/docker/dev.sh stop
```

检查 compose 配置：

```bash
cd deploy/docker
./dev.sh config
```

构建镜像并启动依赖：

```bash
deploy/docker/dev.sh rebuild
deploy/docker/dev.sh status
```

进入开发容器：

```bash
deploy/docker/dev.sh enter
```

源码目录会挂载到容器内 `/workspace`，后续编译和联调命令都可以在该目录下执行。

查看日志：

```bash
deploy/docker/dev.sh logs
deploy/docker/dev.sh logs mysql redis rabbitmq kafka
```

停止环境：

```bash
deploy/docker/dev.sh stop
```

如果需要清空本地数据库和消息中间件数据：

```bash
deploy/docker/dev.sh clean
```

## 5. 常见问题

`docker compose up` 提示端口冲突时，先停止宿主机上已有的 MySQL、Redis、RabbitMQ、ZooKeeper 或 Kafka，
再重新启动 compose。

`dev` 容器里连接依赖失败时，先确认所有服务健康：

```bash
docker compose ps
docker compose logs --tail=100 mysql redis rabbitmq kafka
```

在 Docker Desktop 上使用 host 网络时，需要使用支持 host networking 的版本，并在设置中启用该功能。
Linux Docker Engine 默认支持 host 网络。

## 6. 参考

- Docker Engine Ubuntu 安装文档：https://docs.docker.com/engine/install/ubuntu/
- Docker Engine Linux 安装后配置：https://docs.docker.com/engine/install/linux-postinstall/
- Docker Compose 插件安装文档：https://docs.docker.com/compose/install/linux/
- Docker host 网络文档：https://docs.docker.com/engine/network/drivers/host/
- Docker daemon 代理文档：https://docs.docker.com/engine/daemon/proxy/
- Docker CLI 代理文档：https://docs.docker.com/engine/cli/proxy/
