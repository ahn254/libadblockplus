/*
 * This file is part of Adblock Plus <https://adblockplus.org/>,
 * Copyright (C) 2006-present eyeo GmbH
 *
 * Adblock Plus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Adblock Plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <AdblockPlus.h>
#include <vector>

#include "JsContext.h"
#include "JsError.h"
#include "Utils.h"

using namespace AdblockPlus;

AdblockPlus::JsValue::JsValue(IV8IsolateProviderPtr isolate,
                              v8::Global<v8::Context>* jsContext,
                              v8::Local<v8::Value> value)
    : isolate_(isolate), jsContext_(jsContext), value_(isolate_->Get(), value)
{
}

AdblockPlus::JsValue::JsValue(AdblockPlus::JsValue&& src)
{
  *this = std::move(src);
}

AdblockPlus::JsValue::JsValue(const JsValue& src)
{
  *this = src;
}

AdblockPlus::JsValue::~JsValue()
{
  if (isolate_)
  {
    if (isolate_->Get())
    {
      const JsContext context(isolate_->Get(), *jsContext_);
      value_.Reset();
    }
    else
    {
#if defined(MAKE_ISOLATE_IN_JS_VALUE_WEAK)
      // No isolate/engine. We're being killed after the engine so this leak is
      // a small problem - if unexpected - comparing to the position we're in.
      value_.Empty();
#endif
    }
  }
}

JsValue& AdblockPlus::JsValue::operator=(const JsValue& src)
{
  const JsContext context(src.isolate_->Get(), *src.jsContext_);
  isolate_ = src.isolate_;
  jsContext_ = src.jsContext_;
  value_ = v8::Global<v8::Value>(isolate_->Get(), src.value_);
  return *this;
}

JsValue& AdblockPlus::JsValue::operator=(JsValue&& src)
{
  const JsContext context(src.isolate_->Get(), *src.jsContext_);
  isolate_ = src.isolate_;
  src.isolate_ = nullptr;
  jsContext_ = src.jsContext_;
  src.jsContext_ = nullptr;
  value_ = std::move(src.value_);
  return *this;
}

bool AdblockPlus::JsValue::IsUndefined() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return UnwrapValue()->IsUndefined();
}

bool AdblockPlus::JsValue::IsNull() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return UnwrapValue()->IsNull();
}

bool AdblockPlus::JsValue::IsString() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::Value> value = UnwrapValue();
  return value->IsString() || value->IsStringObject();
}

bool AdblockPlus::JsValue::IsNumber() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::Value> value = UnwrapValue();
  return value->IsNumber() || value->IsNumberObject();
}

bool AdblockPlus::JsValue::IsBool() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::Value> value = UnwrapValue();
  return value->IsBoolean() || value->IsBooleanObject();
}

bool AdblockPlus::JsValue::IsObject() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return UnwrapValue()->IsObject();
}

bool AdblockPlus::JsValue::IsArray() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return UnwrapValue()->IsArray();
}

bool AdblockPlus::JsValue::IsFunction() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return UnwrapValue()->IsFunction();
}

std::string AdblockPlus::JsValue::AsString() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return Utils::FromV8String(isolate_->Get(), UnwrapValue());
}

StringBuffer AdblockPlus::JsValue::AsStringBuffer() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return Utils::StringBufferFromV8String(isolate_->Get(), UnwrapValue());
}

int64_t AdblockPlus::JsValue::AsInt() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  auto currentContext = isolate_->Get()->GetCurrentContext();
  auto value = UnwrapValue()->IntegerValue(currentContext);
  return CHECKED_TO_VALUE(std::move(value));
}

bool AdblockPlus::JsValue::AsBool() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  return UnwrapValue()->BooleanValue(isolate_->Get());
}

double AdblockPlus::JsValue::AsDouble() const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  auto currentContext = isolate_->Get()->GetCurrentContext();
  auto value = UnwrapValue()->NumberValue(currentContext);
  return CHECKED_TO_VALUE(std::move(value));
}

AdblockPlus::JsValueList AdblockPlus::JsValue::AsList() const
{
  if (!IsArray())
    throw std::runtime_error("Cannot convert a non-array to list");

  const JsContext context(isolate_->Get(), *jsContext_);
  auto currentContext = isolate_->Get()->GetCurrentContext();
  JsValueList result;
  v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(UnwrapValue());
  uint32_t length = array->Length();
  for (uint32_t i = 0; i < length; i++)
  {
    v8::Local<v8::Value> item = CHECKED_TO_LOCAL(isolate_->Get(), array->Get(currentContext, i));
    result.push_back(JsValue(isolate_, jsContext_, item));
  }
  return result;
}

std::vector<std::string> AdblockPlus::JsValue::GetOwnPropertyNames() const
{
  if (!IsObject())
    throw std::runtime_error("Attempting to get propert list for a non-object");

  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(UnwrapValue());
  auto propertyNames = CHECKED_TO_LOCAL(
      isolate_->Get(), object->GetOwnPropertyNames(isolate_->Get()->GetCurrentContext()));
  JsValueList properties = JsValue(isolate_, jsContext_, propertyNames).AsList();
  std::vector<std::string> result;
  for (const auto& property : properties)
    result.push_back(property.AsString());
  return result;
}

AdblockPlus::JsValue AdblockPlus::JsValue::GetProperty(const std::string& name) const
{
  if (!IsObject())
    throw std::runtime_error("Attempting to get property of a non-object");

  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::String> property =
      CHECKED_TO_LOCAL(isolate_->Get(), Utils::ToV8String(isolate_->Get(), name));
  v8::Local<v8::Object> obj = v8::Local<v8::Object>::Cast(UnwrapValue());
  return JsValue(
      isolate_,
      jsContext_,
      CHECKED_TO_LOCAL(isolate_->Get(), obj->Get(isolate_->Get()->GetCurrentContext(), property)));
}

void AdblockPlus::JsValue::SetProperty(const std::string& name, v8::Local<v8::Value> val)
{
  if (!IsObject())
    throw std::runtime_error("Attempting to set property on a non-object");

  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::String> property =
      CHECKED_TO_LOCAL(isolate_->Get(), Utils::ToV8String(isolate_->Get(), name));
  v8::Local<v8::Object> obj = v8::Local<v8::Object>::Cast(UnwrapValue());
  CHECKED_TO_VALUE(obj->Set(isolate_->Get()->GetCurrentContext(), property, val));
}

v8::Local<v8::Value> AdblockPlus::JsValue::UnwrapValue() const
{
  return v8::Local<v8::Value>::New(isolate_->Get(), value_);
}

void AdblockPlus::JsValue::SetProperty(const std::string& name, const std::string& val)
{
  const JsContext context(isolate_->Get(), *jsContext_);

  SetProperty(name, CHECKED_TO_LOCAL(isolate_->Get(), Utils::ToV8String(isolate_->Get(), val)));
}

void AdblockPlus::JsValue::SetStringBufferProperty(const std::string& name, const StringBuffer& val)
{
  const JsContext context(isolate_->Get(), *jsContext_);

  SetProperty(name,
              CHECKED_TO_LOCAL(isolate_->Get(), Utils::StringBufferToV8String(isolate_->Get(), val)));
}

void AdblockPlus::JsValue::SetProperty(const std::string& name, int64_t val)
{
  const JsContext context(isolate_->Get(), *jsContext_);
  SetProperty(name, v8::Number::New(isolate_->Get(), val));
}

void AdblockPlus::JsValue::SetProperty(const std::string& name, const JsValue& val)
{
  const JsContext context(isolate_->Get(), *jsContext_);
  SetProperty(name, val.UnwrapValue());
}

void JsValue::SetProperty(const std::string& name, const char* val, size_t size)
{
  const JsContext context(isolate_->Get(), *jsContext_);

  SetProperty(name,
              CHECKED_TO_LOCAL(
                  isolate_->Get(),
                  v8::String::NewFromUtf8(isolate_->Get(), val, v8::NewStringType::kNormal, size)));
}

void AdblockPlus::JsValue::SetProperty(const std::string& name, bool val)
{
  const JsContext context(isolate_->Get(), *jsContext_);
  SetProperty(name, v8::Boolean::New(isolate_->Get(), val));
}

std::string AdblockPlus::JsValue::GetClass() const
{
  if (!IsObject())
    throw std::runtime_error("Cannot get constructor of a non-object");

  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::Object> obj = v8::Local<v8::Object>::Cast(UnwrapValue());
  return Utils::FromV8String(isolate_->Get(), obj->GetConstructorName());
}

JsValue JsValue::Call(const JsValueList& params) const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  std::vector<v8::Local<v8::Value>> argv;
  for (const auto& param : params)
    argv.push_back(param.UnwrapValue());

  return Call(argv, context.GetV8Context()->Global());
}

JsValue JsValue::Call(const JsValueList& params, const JsValue& thisValue) const
{
  const JsContext context(isolate_->Get(), *jsContext_);
  v8::Local<v8::Object> thisObj = v8::Local<v8::Object>::Cast(thisValue.UnwrapValue());

  std::vector<v8::Local<v8::Value>> argv;
  for (const auto& param : params)
    argv.push_back(param.UnwrapValue());

  return Call(argv, thisObj);
}

JsValue JsValue::Call(const JsValue& arg) const
{
  const JsContext context(isolate_->Get(), *jsContext_);

  std::vector<v8::Local<v8::Value>> argv;
  argv.push_back(arg.UnwrapValue());

  return Call(argv, context.GetV8Context()->Global());
}

JsValue JsValue::Call(std::vector<v8::Local<v8::Value>>& args, v8::Local<v8::Object> thisObj) const
{
  if (!IsFunction())
    throw std::runtime_error("Attempting to call a non-function");
  if (!thisObj->IsObject())
    throw std::runtime_error("`this` pointer has to be an object");

  const JsContext context(isolate_->Get(), *jsContext_);

  const v8::TryCatch tryCatch(isolate_->Get());
  v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(UnwrapValue());
  auto result = CHECKED_TO_LOCAL_WITH_TRY_CATCH(isolate_->Get(),
                                                func->Call(isolate_->Get()->GetCurrentContext(),
                                                           thisObj,
                                                           args.size(),
                                                           args.size() ? &args[0] : nullptr),
                                                tryCatch);

  return JsValue(isolate_, jsContext_, result);
}
