/**
 * @file        mqtt-adapter.h 
 * @brief       MQTT Adapter Implementation
 * @details     interfaces with the MQTT broker to write or read data from the broker asynchronously, this is built 
 *              using the PAHO Async C library 
 * @remarks     the async event loop is inside the PAHO Async C library
 * @author      Muthucumaru Maheswaran, Yuxiang Ma
 * @copyright 
 *              Copyright 2020 Muthucumaru Maheswaran, Yuxiang Ma
 * 
 *              Licensed under the Apache License, Version 2.0 (the "License");
 *              you may not use this file except in compliance with the License.
 *              You may obtain a copy of the License at
 * 
 *                  http://www.apache.org/licenses/LICENSE-2.0
 * 
 *              Unless required by applicable law or agreed to in writing, software
 *              distributed under the License is distributed on an "AS IS" BASIS,
 *              WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *              See the License for the specific language governing permissions and
 *              limitations under the License.
 */

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <MQTTAsync.h>
#include <string.h>
#include "mqtt/nvoid.h"
#include "mqtt/mqtt_adapter.h"

void mqtt_default_conn_lost(void *ctx, char *cause)
{
    mqtt_adapter_t *mq = (mqtt_adapter_t *)ctx;
    mq->state = MQTT_ERROR;
    printf("Connection lost at %s reconnecting.. \n", mq->mqtthost);
    sleep(1);
    mqtt_connect(mq);
}

mqtt_adapter_t *mqtt_createserver(char *url, int indx, char *appid, char *devid, void (*onc)(void *)) 
{
    char clientid[64];

    if (url == NULL)
        return NULL;

    mqtt_adapter_t *mq = (mqtt_adapter_t *)calloc(1, sizeof(mqtt_adapter_t));
    mq->onconnect = onc;
    strcpy(mq->mqtthost, url);
    strcpy(mq->app_id, appid);
    sprintf(clientid, "%s-%d-%d", devid, indx, getpid());
    if (MQTTAsync_create(&(mq->mqttserv), url, clientid, MQTTCLIENT_PERSISTENCE_NONE, NULL) != MQTTASYNC_SUCCESS)
    {
        printf("WARNING! Cannot create MQTT endpoint at %s\n", url);        
        free(mq);
        return NULL;
    }
    mqtt_setconnlost(mq, mqtt_default_conn_lost);
    return mq;
}

#define MQTT_DISCONNECT_WAIT    100
void mqtt_deleteserver(mqtt_adapter_t *mq) 
{
    if (mq->state == MQTT_CONNECTED)
        mqtt_disconnect(mq, MQTT_DISCONNECTING);

    while(mq->state != MQTT_NOTCONNECTED)
        usleep(MQTT_DISCONNECT_WAIT);
    MQTTAsync_destroy(&mq->mqttserv);
    free(mq);
}

void mqtt_onconnect(void* context, MQTTAsync_successData* response)
{
    mqtt_adapter_t *mq = (mqtt_adapter_t *)context;

    mq->state = MQTT_CONNECTED;
    for (int i = 0; mq->subscriptions[i] != NULL; i++)
        mqtt_subscribe(mq, mq->subscriptions[i]);
    mq->onconnect(mq);
}

void mqtt_connect(mqtt_adapter_t *mq)
{
    int rc;

    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.onSuccess = mqtt_onconnect;
    conn_opts.context = mq;
    conn_opts.onFailure = NULL;
    rc = MQTTAsync_connect(mq->mqttserv, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS)
    {
        printf("\nERROR! Unable to connect to the MQTT server at %s.\n", mq->mqtthost);
        exit(1);
    }
}

void mqtt_ondisconnect(void* context, MQTTAsync_successData* response)
{
    mqtt_adapter_t *mq = (mqtt_adapter_t *)context;
    if (mq->state == MQTT_DISCONNECTING) {
        mq->state = MQTT_NOTCONNECTED;
        return;
    } 
    mqtt_connect(mq);
}

bool mqtt_disconnect(mqtt_adapter_t *mq, int state)
{
    int rc;

    if (mq->state != MQTT_CONNECTED) 
        return false;

    mq->state = state;
    MQTTAsync_disconnectOptions dconn_opts = MQTTAsync_disconnectOptions_initializer;
    dconn_opts.timeout = 0;    
    dconn_opts.onSuccess = mqtt_ondisconnect;
    dconn_opts.context = mq;
    dconn_opts.onFailure = NULL;

    rc = MQTTAsync_disconnect(mq->mqttserv, &dconn_opts);
    return true;
}

void mqtt_reconnect(mqtt_adapter_t *mq)
{
    int rc;

    rc = MQTTAsync_reconnect(mq->mqttserv);
    if (rc != MQTTASYNC_SUCCESS)
        printf("WARNING!! Unable to reconnect to %s\n", mq->mqtthost);
}

void mqtt_set_subscription(mqtt_adapter_t *mq, char *topic) 
{
    int i = 0;

    while ((mq->subscriptions[i] != NULL) && (i < MAX_SUBSCRIPTIONS))
        i++;
    if (i < MAX_SUBSCRIPTIONS)
        mq->subscriptions[i] = topic;
}

void mqtt_subscribe(mqtt_adapter_t *mq, char *topic)
{
    char fulltopic[128];

    if (mq->state != MQTT_CONNECTED)
    {
        mqtt_set_subscription(mq, topic);
        return;
    }

    sprintf(fulltopic, "/%s%s", mq->app_id, topic);
    if (topic != NULL)
        MQTTAsync_subscribe(mq->mqttserv, fulltopic, 1, NULL);
}

void mqtt_onpublish(void* context, MQTTAsync_successData* response)
{
    nvoid_t *nv = (nvoid_t *)context;
    nvoid_free(nv);
}

bool mqtt_publish(mqtt_adapter_t *mq, char *topic, nvoid_t *nv)
{
    char fulltopic[128];
    sprintf(fulltopic, "/%s%s", mq->app_id, topic);

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = mqtt_onpublish;
    opts.context = nv;

    for(int i = 0; i < MQTT_TIMEOUT && mq->state != MQTT_CONNECTED; i++)
        usleep(100);

    if (mq->state == MQTT_CONNECTED) {
        int rc = MQTTAsync_send(mq->mqttserv, fulltopic, nv->len, nv->data, 1, 0, &opts);
        if (rc != MQTTASYNC_SUCCESS) {
            mq->state = MQTT_ERROR;
            mqtt_disconnect(mq, MQTT_ERROR);
        }
    } else 
        return false;
    return true;
}

void mqtt_setmsgarrived(mqtt_adapter_t *mq, MQTTAsync_messageArrived *ma) 
{
    if (mq->state != MQTT_NOTCONNECTED)
    {
        printf("ERROR! Message Arrived Handler can be set when MQTT is disconnected\n");
        exit(1);
    }
    MQTTAsync_setMessageArrivedCallback(mq->mqttserv, mq, ma);
}

void mqtt_setconnlost(mqtt_adapter_t *mq, MQTTAsync_connectionLost *cl) 
{
    if (mq->state != MQTT_NOTCONNECTED)
    {
        printf("ERROR! Message Arrived Handler can be set when MQTT is disconnected\n");
        exit(1);        
    }
    MQTTAsync_setConnectionLostCallback(mq->mqttserv, mq, cl);
}
