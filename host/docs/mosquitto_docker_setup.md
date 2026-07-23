# Mosquitto MQTT Broker Server Setup on Docker
>**Note:**  This setup performed on Raspberry Pi 4 Model B 4GB<br>
>**OS:** Rocky Linux 10.2

## 1. Install Docker
```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

>**Note:** If you get `Error: Unable to find a match: docker-ce docker-ce-cli docker-ce-rootless-extras` run `sudo dnf install -y dnf-plugins-core` `sudo dnf config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo` `sudo dnf clean all` and `sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin docker-buildx-plugin`

Enable Docker:
```bash
sudo systemctl enable --now docker
```

## 2. Add user to docker group
```bash
sudo usermod -aG docker $USER
```
`logout` or `sudo reboot` to apply the changes:

## 3. Create Directory to store MQTT Brokers data
```bash
sudo mkdir -p /opt/stacks/mqtt
```
Now go in to dir:
```bash
cd /opt/stacks/mqtt
```

## 4. Write Docker File for Mosquitto MQTT Broker
```bash
sudo nano compose.yaml
```

>**Note:** you have to be in /opt/stacks/mqtt

```yaml
services:
  mosquitto:
    image: eclipse-mosquitto
    container_name: mosquitto
    volumes:
      - ./config:/mosquitto/config
      - ./data:/mosquitto/data
      - ./log:/mosquitto/log
    ports:
      - 1883:1883
    stdin_open: true 
    tty: true
```

## 5. Create Mosquitto Config file
Create config folder:
```bash
sudo mkdir config
```
Create the file:
```bash
sudo nano ./config/mosquitto.conf
```
Example config:
```conf
# MQTT Listener
listener 1883 0.0.0.0

# Persistence for retained config messages and broker state
persistence true
persistence_file mosquitto.db
persistence_location /var/lib/mosquitto/

# Development-only authentication
allow_anonymous true

# Logging
log_dest file /var/log/mosquitto/mosquitto.log
log_type error
log_type warning
log_type notice
log_type information
connection_messages true
log_timestamp true
```

## 6. Run the docker container
```bash 
docker compose up -d
```

## 7. Test the MQTT setup
Run the MQTT service on PI:
```bash
docker exec -it mosquitto mosquitto_sub \ 
-h localhost \ 
-p 1883 \ 
-t 'repacss/#' \
-v
```
Then run the following command on another device:
```bash
mosquitto_pub -h 192.168.50.115 -p 1883 \
  -t 'repacss/devices/mac-test/hello' \
  -m '{"message_type":"hello","mac":"mac-test","source":"mac"}'
```
>**TIP:** Make sure that the another device has mosquitto services installed and started



