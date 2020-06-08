#include "mqtt/mqtt_adapter.h"
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>


/*
 * Callback invoked when the client connects to the 
 * MQTT server.
 */
void connected(void *a) {
    // a is a pointer to a mqtt_adapter_t structure.
    printf("Connected to ... mqtt... \n");
}

int msg_arrived(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg)
{
   
    printf("Topic name: %s\n", topicname);
    printf("Message %s\n", (char *)msg->payload);
    // msg is byte array pointed to by payload and has length payloadlen 

    MQTTAsync_freeMessage(&msg);
    MQTTAsync_free(topicname);
    return 1;
}

int main(int argc, char *argv[]) 
{
    nvoid_t *nv;
    char buffer[200];

    if (argc != 2) {
        printf("Usage: mqtt-tester client|server\n\n");
        exit(1);
    }
    mqtt_adapter_t *mq = mqtt_createserver("tcp://localhost:1883", 1, "app-1", "dev-2", connected);
    if (strcmp(argv[1], "client") == 0) {
        mqtt_connect(mq);        
        for (int j = 0; j < 5000000; j++) 
        {   
            sprintf(buffer, "Hello World-%d", j);
            nv = nvoid_new(buffer, strlen(buffer));

            if (mqtt_publish(mq, "/testing-abc", nv) == false) 
                printf("Publishing failed \n");
            sleep(1);        
        }
    } else {
        mqtt_setmsgarrived(mq, msg_arrived);
        mqtt_connect(mq);
        mqtt_set_subscription(mq, "/testing-abc");
    }

    if (strcmp(argv[1], "server") ==0)
        sleep(1000);
    else 
        sleep(100);
    mqtt_deleteserver(mq);
    sleep(1);
}