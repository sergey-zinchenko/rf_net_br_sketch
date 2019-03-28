#include <RH_ASK.h>
#include <Ethernet.h>
#include "base64.hpp"
#include <ArduinoJson.h>

#define MSG_MAX_SIZE 685
#define MAX_CHATS 2
#define MAX_COMPANIONS 2
#define UUID_STR_LEN 37

#define TYPE_FIELD "type"
#define LAYER_FIELD "layer"
#define LOCATION_FIELD "location"
#define CLIENT_FIELD "client"
#define VERSION_INT_FIELD "version_int"
#define CLIENTS_FIELD "clients"
#define COMPANIONS_FIELD "companions"
#define HELLO_FIELD "hello"
#define HI_FIELD "hi"
#define TEXT_FIELD "text"
#define FROM_FIELD "from"
#define TO_FIELD "to"
#define TYPE_WELCOME "welcome"
#define TYPE_DISCONNECTED "disconnected"

#define LAYER_HANDSHAKE "handshake"
#define LAYER_USER "user"
#define LAYER_CHAT "chat"
#define ACCEPT_FIELD "accept"
#define LEAVE_FIELD "leave"
#define START_FIELD "start"
#define DECLINE_FIELD "decline"
#define SIMPLE_MESSAGE "simple_message"

typedef  DynamicJsonDocument* Message;

class EncodedBuffer
{
  private:
    char buf[1024];
    unsigned int bufPos = 0;
    bool rLast = false, bufFrgmt = false;
    void _resetState()
    {
      this->bufPos = 0;
      this->rLast = false;
      this->bufFrgmt = false;
    }
  public:
    char *appendChar(const char chr)
    {
      this->buf[this->bufPos] = chr;
      if (this->rLast && (chr == '\n'))
      {
        if (!this->bufFrgmt) {
          this->buf[this->bufPos] = 0;
          this->buf[this->bufPos - 1] = 0;
          this->_resetState();
          return &(this->buf[0]);
        } else {
          this->_resetState();
          return NULL;
        }
      }
      this->rLast = chr == '\r';
      if (this->bufPos < 1024) {
        this->bufPos++;
      } else {
        this->bufPos = 0;
        this->bufFrgmt = true;
      }
      return NULL;
    }
};

class Base64Buffer: public EncodedBuffer
{
  private:
    char binary[MSG_MAX_SIZE];
  public:
    virtual char *appendChar(const char chr)
    {
      char* encoded = EncodedBuffer::appendChar(chr);
      if (encoded != NULL) {
        unsigned int binary_length = decode_base64(encoded, (unsigned char*)this->binary);
        this->binary[binary_length] = 0;
        return &(this->binary[0]);
      }
      return NULL;
    }
};



class MessageBuffer: public Base64Buffer
{
  private:
    Message doc;
  public:
    MessageBuffer(): Base64Buffer() {
      this->doc = new DynamicJsonDocument(MSG_MAX_SIZE);
    }
    Message operator+=(const char chr)
    {
      char * json = this->appendChar(chr);
      if (json) {
        Serial.print("message received: ");
        Serial.println(json);
        DeserializationError error = deserializeJson(*this->doc, json);
        if (error) {
          Serial.println("json deserialization failed");
          return NULL;
        }
        return this->doc;
      }
      return NULL;
    }
};

class DataSender
{
  protected:
    virtual void sendData(char * data, unsigned int len) = 0;
};

class Base64Encoder : public DataSender
{
  private:
    char binary[1024];
  protected:
    void encodeMessageBase64(const char * data, const size_t len)
    {
      Serial.print("sending message: ");
      Serial.println(data);
      unsigned int base64_length = encode_base64(data, len, binary);
      binary[base64_length] = '\r';
      binary[base64_length + 1] = '\n';
      this->sendData(binary, base64_length + 2);
    }
};

class JsonEncoder : public Base64Encoder
{
  private:
    char json[MSG_MAX_SIZE];
  protected:
    void encodeMessage(const Message message)
    {
      size_t written = serializeJson(*message, json, sizeof(json));
      json[written] = 0;
      this->encodeMessageBase64(json, written);
    }
};

class MessageSender: public JsonEncoder
{
  private:
    Message doc;
  public:
    MessageSender() {
      this->doc = new DynamicJsonDocument(MSG_MAX_SIZE);
    }
  protected:
    void sendHandshakeMessage(char* client, char* nickname, boolean hi)
    {
      Serial.print("sending handshake message to client ");
      Serial.println(client);
      this->doc->clear();
      (*this->doc)[LAYER_FIELD] = LAYER_HANDSHAKE;
      if (hi) {
        (*this->doc)[HI_FIELD] = nickname;
      } else {
        (*this->doc)[HELLO_FIELD] = nickname;
      }
      JsonArray to = this->doc->createNestedArray(TO_FIELD);
      to.add(client);
      this->encodeMessage(this->doc);
    }

    void sendLocationMessage(char* client, char* location)
    {
      Serial.print("sending location message to client ");
      Serial.println(client);
      this->doc->clear();
      (*this->doc)[LAYER_FIELD] = LAYER_USER;
      (*this->doc)[LOCATION_FIELD] = location;
      JsonArray to = this->doc->createNestedArray(TO_FIELD);
      to.add(client);
      this->encodeMessage(this->doc);
    }

    void sendChatAcceptMessage(char* client, char* chat)
    {
      Serial.print("sending chat accepted message for chat ");
      Serial.print(chat);
      Serial.print(" to client ");
      Serial.println(client);
      this->doc->clear();
      (*this->doc)[LAYER_FIELD] = LAYER_CHAT;
      (*this->doc)[ACCEPT_FIELD] = chat;
      JsonArray to = this->doc->createNestedArray(TO_FIELD);
      to.add(client);
      this->encodeMessage(this->doc);
    }

    void sendChatLeaveMessage(char* client, char* chat)
    {
      Serial.print("sending chat left message for chat ");
      Serial.print(chat);
      Serial.print(" to client ");
      Serial.println(client);
      this->doc->clear();
      (*this->doc)[LAYER_FIELD] = LAYER_CHAT;
      (*this->doc)[LEAVE_FIELD] = chat;
      JsonArray to = this->doc->createNestedArray(TO_FIELD);
      to.add(client);
      this->encodeMessage(this->doc);
    }

    void sendChatDeclineMessage(char* client, char* chat)
    {
      Serial.print("sending chat decline message for chat ");
      Serial.print(chat);
      Serial.print(" to client ");
      Serial.println(client);
      this->doc->clear();
      (*this->doc)[LAYER_FIELD] = LAYER_CHAT;
      (*this->doc)[DECLINE_FIELD] = chat;
      JsonArray to = this->doc->createNestedArray(TO_FIELD);
      to.add(client);
      this->encodeMessage(this->doc);
    }

    void sendChatSimpleMessage(char* client, char* chat, char*  text)
    {
      Serial.print("sending simple message for chat ");
      Serial.print(chat);
      Serial.print(" to client ");
      Serial.println(client);
      this->doc->clear();
      (*this->doc)[LAYER_FIELD] = LAYER_CHAT;
      (*this->doc)[SIMPLE_MESSAGE] = chat;
      (*this->doc)[TEXT_FIELD] = text;
      JsonArray to = this->doc->createNestedArray(TO_FIELD);
      to.add(client);
      this->encodeMessage(this->doc);
    }
};

class MChatCore: public MessageSender
{
  private:
    IPAddress remoteAddress;
    unsigned int remotePort;
    EthernetClient client;
    MessageBuffer messageBuffer;
    int version;

    bool checkConnection() {
      if (this->client.connected())
        return true;
      Serial.println("trying to connect to remote server");
      if (this->client.connect(remoteAddress, remotePort)) {
        Serial.println("connection established");
        return true;
      } else {
        Serial.println("failed to connect to the server");
        delay(5000);
        return false;
      }
    }

    Message receiveMessage()
    {
      if (this->client.available() > 0) {
        return this->messageBuffer += this->client.read();
      } else {
        return NULL;
      }
    }

    void processMessage(Message msg)
    {
      char *type = (*msg)[TYPE_FIELD];
      char *layer = (*msg)[LAYER_FIELD];
      if (type) {
        if (strcmp(type, TYPE_WELCOME) == 0) {
          char* clients[MAX_COMPANIONS];
          int i;
          JsonArray arr = (*msg)[CLIENTS_FIELD].as<JsonArray>();
          for (i = 0; i < min(arr.size(), MAX_COMPANIONS); i++)
          {
            clients[i] = arr[i].as<const char *>();
          }
          this->onWelcome((*msg)[VERSION_INT_FIELD].as<int>(), clients, i);
        } else if (strcmp(type, TYPE_DISCONNECTED) == 0) {
          this->onClientDisconnected( (*msg)[CLIENT_FIELD]);
        }
      } else if (layer) {
        if (strcmp(layer, LAYER_HANDSHAKE) == 0) {
          this->onHandshake((*msg)[FROM_FIELD], (*msg)[HI_FIELD].isNull() ? (*msg)[HELLO_FIELD] : (*msg)[HI_FIELD], (*msg)[HELLO_FIELD].isNull());
        } else if (strcmp(layer, LAYER_CHAT) == 0) {
          char *start = (*msg)[START_FIELD];
          char *accept =  (*msg)[ACCEPT_FIELD];
          char *leave = (*msg)[LEAVE_FIELD];
          char *decline = (*msg)[DECLINE_FIELD];
          if (start) {
            char* companions[MAX_COMPANIONS];
            int i;
            JsonArray arr = (*msg)[COMPANIONS_FIELD].as<JsonArray>();
            for (i = 0; i < min(arr.size(), MAX_COMPANIONS); i++)
            {
              companions[i] = arr[i].as<const char *>();
            }
            this->onChatStart(&companions[0], i, (*msg)[FROM_FIELD].as<const char *>(), start);
          } else if (leave) {
            this->onChatLeave((*msg)[FROM_FIELD].as<const char *>(), leave);
          } else if (accept) {
            this->onChatAccept((*msg)[FROM_FIELD].as<const char *>(), accept);
          } else if (decline) {
            this->onChatDecline((*msg)[FROM_FIELD].as<const char *>(), decline);
          }
        }
      }
    }

    void onWelcome(int version, char** clients, unsigned int count)
    {
      Serial.print("welcome from server version ");
      Serial.print(version);
      Serial.print(" with ");
      Serial.print(count);
      Serial.println(" clients:");
      for (unsigned int i = 0; i < count; i++)
      {
        Serial.println(clients[i]);
      }
      this->version = version;
      for (unsigned int j = 0; j < count; j++)
      {
        this->sendHandshakeMessage(clients[j], this->nickname, false);
      }
    }

    void onHandshake(const char* client, const char* nickname, bool done)
    {
      Serial.print("handshake from client ");
      Serial.print(client);
      Serial.print(" nickname ");
      Serial.println(nickname);
      if (!done)
      {
        this->sendHandshakeMessage(client, this->nickname, true);
        this->sendLocationMessage(client, this->location);
        this->sendChatDeclineMessage(client, "00000000-0000-0000-0000-000000000000");
      } else
        this->sendLocationMessage(client, this->location);
      this->sendChatDeclineMessage(client, "00000000-0000-0000-0000-000000000000");
    }

  protected:
    char* nickname;
    char* location;

    virtual void sendData(char * data, unsigned int len)
    {
      this->client.write(data, len);
      this->client.flush();
    }

    virtual void onClientDisconnected(const char* client)
    {
      Serial.print("client disconnected: ");
      Serial.println(client);
    }

    virtual void onChatStart(char** companions, unsigned int count, char* client, char* chat)
    {
      Serial.print("chat with id ");
      Serial.print(chat);
      Serial.print(" invitation from ");
      Serial.print(client);
      if (count > 0)
      {
        Serial.print(" with ");
        Serial.print(count);
        Serial.println(" companions:");
        for (unsigned int i = 0; i < count; i++)
        {
          Serial.println(companions[i]);
        }
      } else
        Serial.println("");
    }

    virtual void onChatLeave(char* client, char* chat)
    {
      Serial.print("chat with id ");
      Serial.print(chat);
      Serial.print(" left by ");
      Serial.println(client);
    }

    virtual void onChatAccept(char* client, char* chat)
    {
      Serial.print("chat with id ");
      Serial.print(chat);
      Serial.print(" accepted by ");
      Serial.println(client);
    }

    virtual void onChatDecline(char* client, char* chat)
    {
      Serial.print("chat with id ");
      Serial.print(chat);
      Serial.print(" declined by ");
      Serial.println(client);
    }


  public:
    MChatCore(const char nickname[], const char location[], const IPAddress remoteAddress, const unsigned int remotePort): MessageSender()
    {
      this->nickname = nickname;
      this->location = location;
      this->remoteAddress = remoteAddress;
      this->remotePort = remotePort;
    }

    void doStuff()
    {
      if (!this->checkConnection())
        return;
      Message msg = this->receiveMessage();
      if (msg == NULL) {
        return;
      }
      this->processMessage(msg);
    }

};

class MyCore;

enum CompanionState
{
  Pending, Accepted
};

struct Companion
{
  char id[UUID_STR_LEN];
  CompanionState state;
};

class Chat
{
  private:
    MyCore* core;
    char id[UUID_STR_LEN];
    Companion companions[MAX_COMPANIONS];
    unsigned int companionsCount;
  public:
    bool checkId(const char* id)
    {
      return strcasestr(this->id, id) != NULL;
    }

    void clear()
    {
      this->companionsCount = 0;
      this->core = NULL;
    }

    void init(MyCore* core, const char* id, const char* organizator, const char** companions, unsigned int companionsCount)
    {
      Serial.println("initing chat");
      this->companionsCount = 0;
      this->core = core;
      strcpy(this->id, id);
      for (unsigned int i = 0; i < min(MAX_COMPANIONS - 1, companionsCount); i++)
      {
        strcpy(this->companions[i].id, companions[i]);
        this->companions[i].state = Pending;
        this->companionsCount++;
      }
      strcpy(this->companions[this->companionsCount].id, organizator);
      this->companions[this->companionsCount].state = Accepted;
      this->companionsCount++;

      Serial.println("companions pending:");

      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Pending)
        {
          Serial.println(this->companions[i].id);
        }
      }

      Serial.println("companions accepted:");
      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Accepted)
        {
          Serial.println(this->companions[i].id);
        }
      }

    }



    bool companionLeft(char* companion)
    {

      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (strcasestr(companion, companions[i].id) != NULL)
        {
          Serial.println("companion left chat");
          for (unsigned int j = i + 1; j < this->companionsCount; j ++ )
          {
            companions[j - 1] = companions[j];
          }
          companionsCount--;
          break;
        }
      }

      Serial.println("companions pending:");

      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Pending)
        {
          Serial.println(this->companions[i].id);
        }
      }

      Serial.println("companions accepted:");
      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Accepted)
        {
          Serial.println(this->companions[i].id);
        }
      }


      return this->companionsCount == 0;
    }

    void companionAccepted(char* companion)
    {

      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (strcasestr(companion, this->companions[i].id) != NULL)
        {
          Serial.println("companion accepted chat");
          this->companions[i].state = Accepted;
          break;
        }
      }

      Serial.println("companions pending:");

      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Pending)
        {
          Serial.println(this->companions[i].id);
        }
      }

      Serial.println("companions accepted:");
      for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Accepted)
        {
          Serial.println(this->companions[i].id);
        }
      }

    }

    void sendMessage(const char* text);

   
};


class MyCore: public MChatCore
{
  private:
    Chat chats[MAX_CHATS];
    unsigned int chatCount;

  public:

    void sendToAllChats(char*  text)
    {
       for (unsigned int i = 0; i < this->chatCount; i ++)
      {
        this->chats[i].sendMessage(text);
      }
    }
  
    void chatSimpleMessage(char* client, char* chat, char*  text)
    {   
        MChatCore::sendChatSimpleMessage(client, chat, text);
    }
    
    MyCore(const char nickname[], const char location[]): MChatCore(nickname, location, IPAddress(185, 78, 116, 71), 9000)
    {

    }

  protected:
    virtual void onClientDisconnected(const char* client)
    {
      MChatCore::onClientDisconnected(client);
      for (unsigned int i = 0; i < this->chatCount; i ++)
      {
        if (this->chats[i].companionLeft(client))
        {
          Serial.println("chat ended");
          chats[i].clear();
          for (unsigned int j = i + 1; j < chatCount; j ++)
          {
            this->chats[j - 1] = this->chats[j];
          }
          chatCount--;
          i--;
        }
      }
    }

    virtual void onChatStart(char** companions, unsigned int count, char* client, char* chat)
    {
      MChatCore::onChatStart(companions, count, client, chat);
      if (chatCount < MAX_CHATS)
      {
        this->chats[this->chatCount].init(this, chat, client, companions, count);
        this->chatCount++;
        for (unsigned int i = 0; i < min(MAX_COMPANIONS - 1, count); i++)
        {
          this->sendChatAcceptMessage(companions[i], chat);
        }
        this->sendChatAcceptMessage(client, chat);
        this->chatStarted(&this->chats[this->chatCount - 1]);
      } else {
        this->sendChatDeclineMessage(client, chat);
        for (unsigned int i = 0; i <  count; i++)
        {
           this->sendChatDeclineMessage(companions[i], chat);
        }
      }
    }

    virtual void chatStarted(Chat *chat)
    {
      Serial.println("chat started");
    }

    virtual void onChatLeave(char* client, char* chat)
    {
      MChatCore::onChatLeave(client, chat);
      for (unsigned int i = 0; i < this->chatCount; i ++)
      {
        if (this->chats[i].checkId(chat))
        {
          if (this->chats[i].companionLeft(client))
          {
            Serial.println("chat ended");
            chats[i].clear();
            for (unsigned int j = i + 1; j < chatCount; j ++)
            {
              this->chats[j - 1] = this->chats[j];
            }
            this->chatCount--;
            i--;
          }
          break;
        }
      }
    }

    virtual void onChatAccept(char* client, char* chat)
    {
      MChatCore::onChatAccept(client, chat);
      for (unsigned int i = 0; i < this->chatCount; i ++)
      {
        if (this->chats[i].checkId(chat))
        {
          this->chats[i].companionAccepted(client);
          break;
        }
      }

    }

    virtual void onChatDecline(char* client, char* chat)
    {
      MChatCore::onChatDecline(client, chat);
      for (unsigned int i = 0; i < this->chatCount; i ++)
      {
        if (this->chats[i].checkId(chat))
        {
          if (this->chats[i].companionLeft(client))
          {
            Serial.println("chat ended");
            chats[i].clear();
            for (unsigned int j = i + 1; j < chatCount; j ++)
            {
              this->chats[j - 1] = this->chats[j];
            }
            this->chatCount--;
            i--;
          }
          break;
        }
      }
    }
};

 void Chat::sendMessage(const char* text)
    {
       for (unsigned int i = 0; i < this->companionsCount; i++)
      {
        if (this->companions[i].state == Accepted)
        {
          this->core->chatSimpleMessage(&(this->companions[i].id[0]), &(this->id[0]), text);
        }
      }
    }

RH_ASK rf(2000, 9);

byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xFF, 0xFE, 0xFD
};

MyCore core("weather sensors", "in the room on a sofa");

void setup()
{
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("arduino rf to chat bridge starting");
  while (Ethernet.begin(mac) == 0) {
    Serial.println("failed to configure ethernet using dhcp");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("ethernet shield not found");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("ethernet cable not connected.");
    }
    delay(1000);
  }
  Serial.println("ethernet confugured using dhcp");
  Serial.print("ip address: ");
  Serial.println(Ethernet.localIP());
  while (!rf.init()) {
    Serial.println("failed to configure rf");
    delay(1000);
  }
  Serial.println("rf configured");
}

bool renewAdreess()
{
  switch (Ethernet.maintain()) {
    case 1:
      Serial.println("renewed failed");
      return false;

    case 2:
      Serial.println("renewed succeded");
      Serial.print("ip address: ");
      Serial.println(Ethernet.localIP());
      return true;

    case 3:
      Serial.println("rebind failed");
      return false;

    sase 4:
      Serial.println("rebind succeded");
      Serial.print("ip address: ");
      Serial.println(Ethernet.localIP());
      return true;

    default:
      return true;
  }
}

void loop()
{
  if (!renewAdreess()) {
    return;
  }
  core.doStuff();
  uint8_t buf[26];
  uint8_t buflen = sizeof(buf);
  memset(buf, 0, buflen);
  if (rf.recv(buf, &buflen))
  {
    Serial.println((char*)buf);
    core.sendToAllChats((char*)buf);
  }
}
