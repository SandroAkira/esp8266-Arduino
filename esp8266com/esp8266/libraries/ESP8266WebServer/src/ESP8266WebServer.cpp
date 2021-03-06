/*
  ESP8266WebServer.cpp - Dead simple web-server.
  Supports only one simultaneous client, knows how to handle GET and POST.

  Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
  Modified 8 May 2015 by Hristo Gochkov (proper post and file upload handling)
*/


#include <Arduino.h>
#include "WiFiServer.h"
#include "WiFiClient.h"
#include "ESP8266WebServer.h"

// #define DEBUG
#define DEBUG_OUTPUT Serial

struct ESP8266WebServer::RequestHandler {
  RequestHandler(ESP8266WebServer::THandlerFunction fn, const char* uri, HTTPMethod method)
  : fn(fn)
  , uri(uri)
  , method(method)
  , next(NULL)
  {
  }

  ESP8266WebServer::THandlerFunction fn;
  String uri;
  HTTPMethod method;
  RequestHandler* next;

};

ESP8266WebServer::ESP8266WebServer(int port)
: _server(port)
, _firstHandler(0)
, _lastHandler(0)
, _currentArgCount(0)
, _currentArgs(0)
{
}

ESP8266WebServer::~ESP8266WebServer()
{
  if (!_firstHandler)
    return;
  RequestHandler* handler = _firstHandler;
  while (handler) {
    RequestHandler* next = handler->next;
    delete handler;
    handler = next;
  }
}

void ESP8266WebServer::begin() {
  _server.begin();
}


void ESP8266WebServer::on(const char* uri, ESP8266WebServer::THandlerFunction handler)
{
  on(uri, HTTP_ANY, handler);
}

void ESP8266WebServer::on(const char* uri, HTTPMethod method, ESP8266WebServer::THandlerFunction fn)
{
  RequestHandler* handler = new RequestHandler(fn, uri, method);
  if (!_lastHandler) {
    _firstHandler = handler;
    _lastHandler = handler;
  }
  else {
    _lastHandler->next = handler;
    _lastHandler = handler;
  }
}

void ESP8266WebServer::handleClient()
{
  WiFiClient client = _server.available();
  if (!client) {
    return;
  }

#ifdef DEBUG
  DEBUG_OUTPUT.println("New client");
#endif

  // Wait for data from client to become available
  while(client.connected() && !client.available()){
    delay(1);
  }

  if (!_parseRequest(client)) {
    return;
  }

  _currentClient = client;
  _handleRequest();
}

void ESP8266WebServer::sendHeader(String name, String value, bool first) {
  String headerLine = name;
  headerLine += ": ";
  headerLine += value;
  headerLine += "\r\n";

  if (first) {
    _responseHeaders = headerLine + _responseHeaders;
  }
  else {
    _responseHeaders += headerLine;
  }
}

void ESP8266WebServer::send(int code, const char* content_type, String content) {
  String response = "HTTP/1.1 ";
  response += String(code);
  response += " ";
  response += _responseCodeToString(code);
  response += "\r\n";

  if (!content_type)
    content_type = "text/html";
  sendHeader("Content-Type", content_type, true);

  response += _responseHeaders;
  response += "\r\n";
  response += content;
  _responseHeaders = String();
  sendContent(response);
}

void ESP8266WebServer::sendContent(String content) {
  size_t size_to_send = content.length();
  size_t size_sent = 0;
  while(size_to_send) {
    const size_t unit_size = HTTP_DOWNLOAD_UNIT_SIZE;
    size_t will_send = (size_to_send < unit_size) ? size_to_send : unit_size;
    size_t sent = _currentClient.write(content.c_str() + size_sent, will_send);
    size_to_send -= sent;
    size_sent += sent;
    if (sent == 0) {
      break;
    }
  }
}

String ESP8266WebServer::arg(const char* name) {
  for (int i = 0; i < _currentArgCount; ++i) {
    if (_currentArgs[i].key == name)
      return _currentArgs[i].value;
  }
  return String();
}

String ESP8266WebServer::arg(int i) {
  if (i < _currentArgCount)
    return _currentArgs[i].value;
  return String();
}

String ESP8266WebServer::argName(int i) {
  if (i < _currentArgCount)
    return _currentArgs[i].key;
  return String();
}

int ESP8266WebServer::args() {
  return _currentArgCount;
}

bool ESP8266WebServer::hasArg(const char* name) {
  for (int i = 0; i < _currentArgCount; ++i) {
    if (_currentArgs[i].key == name)
      return true;
  }
  return false;
}

void ESP8266WebServer::onFileUpload(THandlerFunction fn) {
  _fileUploadHandler = fn;
}

void ESP8266WebServer::onNotFound(THandlerFunction fn) {
  _notFoundHandler = fn;
}

void ESP8266WebServer::_handleRequest() {
  RequestHandler* handler;
  for (handler = _firstHandler; handler; handler = handler->next)
  {
    if (handler->method != HTTP_ANY && handler->method != _currentMethod)
      continue;

    if (handler->uri != _currentUri)
      continue;

    handler->fn();
    break;
  }

  if (!handler){
#ifdef DEBUG
    DEBUG_OUTPUT.println("request handler not found");
#endif

    if(_notFoundHandler) {
      _notFoundHandler();
    }
    else {
      send(404, "text/plain", String("Not found: ") + _currentUri);
    }
  }

  _currentClient   = WiFiClient();
  _currentUri      = String();
}

const char* ESP8266WebServer::_responseCodeToString(int code) {
  switch (code) {
    case 200: return "OK";
    case 404: return "Not found";
    case 500: return "Fail";
    default:  return "";
  }
}