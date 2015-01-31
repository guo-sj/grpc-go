/*
 *
 * Copyright 2014, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "./go_generator.h"

#include <cctype>

#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor.h>

using namespace std;

namespace grpc_go_generator {

static map<string, string> g_import_alias;
static set<string> g_imports;

bool NoStreaming(const google::protobuf::MethodDescriptor* method) {
  return !method->client_streaming() && !method->server_streaming();
}

bool ClientOnlyStreaming(const google::protobuf::MethodDescriptor* method) {
  return method->client_streaming() && !method->server_streaming();
}

bool ServerOnlyStreaming(const google::protobuf::MethodDescriptor* method) {
  return !method->client_streaming() && method->server_streaming();
}

bool BidiStreaming(const google::protobuf::MethodDescriptor* method) {
  return method->client_streaming() && method->server_streaming();
}

bool HasClientOnlyStreaming(const google::protobuf::FileDescriptor* file) {
  for (int i = 0; i < file->service_count(); i++) {
    for (int j = 0; j < file->service(i)->method_count(); j++) {
      if (ClientOnlyStreaming(file->service(i)->method(j))) {
        return true;
      }
    }
  }
  return false;
}

string LowerCaseService(const string& service) {
  string ret = service;
  if (!ret.empty() && ret[0] >= 'A' && ret[0] <= 'Z') {
    ret[0] = ret[0] - 'A' + 'a';
  }
  return ret;
}

std::string BadToUnderscore(std::string str) {
  for (unsigned i = 0; i < str.size(); ++i) {
    if (!std::isalnum(str[i])) {
      str[i] = '_';
    }
  }
  return str;
}

string GenerateFullPackage(const google::protobuf::FileDescriptor* file) {
  // In opensouce environment, assume each directory has at most one package.
  size_t pos = file->name().find_last_of('/');
  if (pos != string::npos) {
    return file->name().substr(0, pos);
  }
  return "";
}

const string GetFullName(const google::protobuf::Descriptor* desc) {
  string pkg = GenerateFullPackage(desc->file());
  if (g_imports.find(pkg) == g_imports.end()) {
    return desc->name();
  }
  if (g_import_alias.find(pkg) != g_import_alias.end()) {
    return g_import_alias[pkg] + "." + desc->name();
  }
  return BadToUnderscore(desc->file()->package()) + "." + desc->name();
}

void PrintClientMethodDef(google::protobuf::io::Printer* printer,
                          const google::protobuf::MethodDescriptor* method,
                          map<string, string>* vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = GetFullName(method->input_type());
  (*vars)["Response"] = GetFullName(method->output_type());
  if (NoStreaming(method)) {
    printer->Print(*vars,
                   "\t$Method$(ctx context.Context, in *$Request$, opts "
                   "...rpc.CallOption) "
                   "(*$Response$, error)\n");
  } else if (BidiStreaming(method)) {
    printer->Print(*vars,
                   "\t$Method$(ctx context.Context, opts ...rpc.CallOption) "
                   "($Service$_$Method$Client, error)\n");
  } else if (ServerOnlyStreaming(method)) {
    printer->Print(
        *vars,
        "\t$Method$(ctx context.Context, m *$Request$, opts ...rpc.CallOption) "
        "($Service$_$Method$Client, error)\n");
  } else if (ClientOnlyStreaming(method)) {
    printer->Print(*vars,
                   "\t$Method$(ctx context.Context, opts ...rpc.CallOption) "
                   "($Service$_$Method$Client, error)\n");
  }
}

void PrintClientMethodImpl(google::protobuf::io::Printer* printer,
                           const google::protobuf::MethodDescriptor* method,
                           map<string, string>* vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = GetFullName(method->input_type());
  (*vars)["Response"] = GetFullName(method->output_type());
  if (NoStreaming(method)) {
    printer->Print(
        *vars,
        "func (c *$ServiceStruct$Client) $Method$(ctx context.Context, "
        "in *$Request$, opts ...rpc.CallOption) (*$Response$, error) {\n");
    printer->Print(*vars, "\tout := new($Response$)\n");
    printer->Print(*vars,
                   "\terr := rpc.Invoke(ctx, \"/$Package$$Service$/$Method$\", "
                   "in, out, c.cc, opts...)\n");
    printer->Print("\tif err != nil {\n");
    printer->Print("\t\treturn nil, err\n");
    printer->Print("\t}\n");
    printer->Print("\treturn out, nil\n");
    printer->Print("}\n\n");
  } else if (BidiStreaming(method)) {
    printer->Print(
        *vars,
        "func (c *$ServiceStruct$Client) $Method$(ctx context.Context, opts "
        "...rpc.CallOption) ($Service$_$Method$Client, error) {\n"
        "\tstream, err := rpc.NewClientStream(ctx, c.cc, "
        "\"/$Package$$Service$/$Method$\", opts...)\n"
        "\tif err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn &$ServiceStruct$$Method$Client{stream}, nil\n"
        "}\n\n");
    printer->Print(*vars,
                   "type $Service$_$Method$Client interface {\n"
                   "\tSend(*$Request$) error\n"
                   "\tRecv() (*$Response$, error)\n"
                   "\trpc.ClientStream\n"
                   "}\n\n");
    printer->Print(*vars,
                   "type $ServiceStruct$$Method$Client struct {\n"
                   "\trpc.ClientStream\n"
                   "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Client) Send(m *$Request$) error {\n"
        "\treturn x.ClientStream.SendProto(m)\n"
        "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Client) Recv() (*$Response$, error) "
        "{\n"
        "\tm := new($Response$)\n"
        "\tif err := x.ClientStream.RecvProto(m); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn m, nil\n"
        "}\n\n");
  } else if (ServerOnlyStreaming(method)) {
    printer->Print(
        *vars,
        "func (c *$ServiceStruct$Client) $Method$(ctx context.Context, m "
        "*$Request$, "
        "opts ...rpc.CallOption) ($Service$_$Method$Client, error) {\n"
        "\tstream, err := rpc.NewClientStream(ctx, c.cc, "
        "\"/$Package$$Service$/$Method$\", opts...)\n"
        "\tif err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\tx := &$ServiceStruct$$Method$Client{stream}\n"
        "\tif err := x.ClientStream.SendProto(m); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\tif err := x.ClientStream.CloseSend(); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn x, nil\n"
        "}\n\n");
    printer->Print(*vars,
                   "type $Service$_$Method$Client interface {\n"
                   "\tRecv() (*$Response$, error)\n"
                   "\trpc.ClientStream\n"
                   "}\n\n");
    printer->Print(*vars,
                   "type $ServiceStruct$$Method$Client struct {\n"
                   "\trpc.ClientStream\n"
                   "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Client) Recv() (*$Response$, error) "
        "{\n"
        "\tm := new($Response$)\n"
        "\tif err := x.ClientStream.RecvProto(m); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn m, nil\n"
        "}\n\n");
  } else if (ClientOnlyStreaming(method)) {
    printer->Print(
        *vars,
        "func (c *$ServiceStruct$Client) $Method$(ctx context.Context, opts "
        "...rpc.CallOption) ($Service$_$Method$Client, error) {\n"
        "\tstream, err := rpc.NewClientStream(ctx, c.cc, "
        "\"/$Package$$Service$/$Method$\", opts...)\n"
        "\tif err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn &$ServiceStruct$$Method$Client{stream}, nil\n"
        "}\n\n");
    printer->Print(*vars,
                   "type $Service$_$Method$Client interface {\n"
                   "\tSend(*$Request$) error\n"
                   "\tCloseAndRecv() (*$Response$, error)\n"
                   "\trpc.ClientStream\n"
                   "}\n\n");
    printer->Print(*vars,
                   "type $ServiceStruct$$Method$Client struct {\n"
                   "\trpc.ClientStream\n"
                   "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Client) Send(m *$Request$) error {\n"
        "\treturn x.ClientStream.SendProto(m)\n"
        "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Client) CloseAndRecv() (*$Response$, "
        "error) {\n"
        "\tif err := x.ClientStream.CloseSend(); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\tm := new($Response$)\n"
        "\tif err := x.ClientStream.RecvProto(m); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\t// Read EOF.\n"
        "\tif err := x.ClientStream.RecvProto(m); err == io.EOF {\n"
        "\t\treturn m, io.EOF\n"
        "\t}\n"
        "\t// gRPC protocol violation.\n"
        "\treturn m, fmt.Errorf(\"Violate gRPC client streaming protocol: no "
        "EOF after the response.\")\n"
        "}\n\n");
  }
}

void PrintClient(google::protobuf::io::Printer* printer,
                 const google::protobuf::ServiceDescriptor* service,
                 map<string, string>* vars) {
  (*vars)["Service"] = service->name();
  (*vars)["ServiceStruct"] = LowerCaseService(service->name());
  printer->Print(*vars, "type $Service$Client interface {\n");
  for (int i = 0; i < service->method_count(); ++i) {
    PrintClientMethodDef(printer, service->method(i), vars);
  }
  printer->Print("}\n\n");

  printer->Print(*vars,
                 "type $ServiceStruct$Client struct {\n"
                 "\tcc *rpc.ClientConn\n"
                 "}\n\n");
  printer->Print(
      *vars,
      "func New$Service$Client(cc *rpc.ClientConn) $Service$Client {\n"
      "\treturn &$ServiceStruct$Client{cc}\n"
      "}\n\n");
  for (int i = 0; i < service->method_count(); ++i) {
    PrintClientMethodImpl(printer, service->method(i), vars);
  }
}

void PrintServerMethodDef(google::protobuf::io::Printer* printer,
                          const google::protobuf::MethodDescriptor* method,
                          map<string, string>* vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = GetFullName(method->input_type());
  (*vars)["Response"] = GetFullName(method->output_type());
  if (NoStreaming(method)) {
    printer->Print(
        *vars,
        "\t$Method$(context.Context, *$Request$) (*$Response$, error)\n");
  } else if (BidiStreaming(method)) {
    printer->Print(*vars, "\t$Method$($Service$_$Method$Server) error\n");
  } else if (ServerOnlyStreaming(method)) {
    printer->Print(*vars,
                   "\t$Method$(*$Request$, $Service$_$Method$Server) error\n");
  } else if (ClientOnlyStreaming(method)) {
    printer->Print(*vars, "\t$Method$($Service$_$Method$Server) error\n");
  }
}

void PrintServerHandler(google::protobuf::io::Printer* printer,
                        const google::protobuf::MethodDescriptor* method,
                        map<string, string>* vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = GetFullName(method->input_type());
  (*vars)["Response"] = GetFullName(method->output_type());
  if (NoStreaming(method)) {
    printer->Print(
        *vars,
        "func _$Service$_$Method$_Handler(srv interface{}, ctx context.Context,"
        " buf []byte) (proto.Message, error) {\n");
    printer->Print(*vars, "\tin := new($Request$)\n");
    printer->Print("\tif err := proto.Unmarshal(buf, in); err != nil {\n");
    printer->Print("\t\treturn nil, err\n");
    printer->Print("\t}\n");
    printer->Print(*vars,
                   "\tout, err := srv.($Service$Server).$Method$(ctx, in)\n");
    printer->Print("\tif err != nil {\n");
    printer->Print("\t\treturn nil, err\n");
    printer->Print("\t}\n");
    printer->Print("\treturn out, nil\n");
    printer->Print("}\n\n");
  } else if (BidiStreaming(method)) {
    printer->Print(
        *vars,
        "func _$Service$_$Method$_Handler(srv interface{}, stream rpc.ServerStream) "
        "error {\n"
        "\treturn srv.($Service$Server).$Method$(&$ServiceStruct$$Method$Server"
        "{stream})\n"
        "}\n\n");
    printer->Print(*vars,
                   "type $Service$_$Method$Server interface {\n"
                   "\tSend(*$Response$) error\n"
                   "\tRecv() (*$Request$, error)\n"
                   "\trpc.ServerStream\n"
                   "}\n\n");
    printer->Print(*vars,
                   "type $ServiceStruct$$Method$Server struct {\n"
                   "\trpc.ServerStream\n"
                   "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Server) Send(m *$Response$) error {\n"
        "\treturn x.ServerStream.SendProto(m)\n"
        "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Server) Recv() (*$Request$, error) "
        "{\n"
        "\tm := new($Request$)\n"
        "\tif err := x.ServerStream.RecvProto(m); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn m, nil\n"
        "}\n\n");
  } else if (ServerOnlyStreaming(method)) {
    printer->Print(
        *vars,
        "func _$Service$_$Method$_Handler(srv interface{}, stream rpc.ServerStream) "
        "error {\n"
        "\tm := new($Request$)\n"
        "\tif err := stream.RecvProto(m); err != nil {\n"
        "\t\treturn err\n"
        "\t}\n"
        "\treturn srv.($Service$Server).$Method$(m, "
        "&$ServiceStruct$$Method$Server{stream})\n"
        "}\n\n");
    printer->Print(*vars,
                   "type $Service$_$Method$Server interface {\n"
                   "\tSend(*$Response$) error\n"
                   "\trpc.ServerStream\n"
                   "}\n\n");
    printer->Print(*vars,
                   "type $ServiceStruct$$Method$Server struct {\n"
                   "\trpc.ServerStream\n"
                   "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Server) Send(m *$Response$) error {\n"
        "\treturn x.ServerStream.SendProto(m)\n"
        "}\n\n");
  } else if (ClientOnlyStreaming(method)) {
    printer->Print(
        *vars,
        "func _$Service$_$Method$_Handler(srv interface{}, stream rpc.ServerStream) "
        "error {\n"
        "\treturn srv.($Service$Server).$Method$(&$ServiceStruct$$Method$Server"
        "{stream})\n"
        "}\n\n");
    printer->Print(*vars,
                   "type $Service$_$Method$Server interface {\n"
                   "\tSendAndClose(*$Response$) error\n"
                   "\tRecv() (*$Request$, error)\n"
                   "\trpc.ServerStream\n"
                   "}\n\n");
    printer->Print(*vars,
                   "type $ServiceStruct$$Method$Server struct {\n"
                   "\trpc.ServerStream\n"
                   "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Server) SendAndClose(m *$Response$) "
        "error {\n"
        "\tif err := x.ServerStream.SendProto(m); err != nil {\n"
        "\t\treturn err\n"
        "\t}\n"
        "\treturn nil\n"
        "}\n\n");
    printer->Print(
        *vars,
        "func (x *$ServiceStruct$$Method$Server) Recv() (*$Request$, error) {\n"
        "\tm := new($Request$)\n"
        "\tif err := x.ServerStream.RecvProto(m); err != nil {\n"
        "\t\treturn nil, err\n"
        "\t}\n"
        "\treturn m, nil\n"
        "}\n\n");
  }
}

void PrintServerMethodDesc(google::protobuf::io::Printer* printer,
                           const google::protobuf::MethodDescriptor* method,
                           map<string, string>* vars) {
  (*vars)["Method"] = method->name();
  printer->Print("\t\t{\n");
  printer->Print(*vars, "\t\t\tMethodName:\t\"$Method$\",\n");
  printer->Print(*vars, "\t\t\tHandler:\t_$Service$_$Method$_Handler,\n");
  printer->Print("\t\t},\n");
}

void PrintServerStreamingMethodDesc(
    google::protobuf::io::Printer* printer,
    const google::protobuf::MethodDescriptor* method,
    map<string, string>* vars) {
  (*vars)["Method"] = method->name();
  printer->Print("\t\t{\n");
  printer->Print(*vars, "\t\t\tStreamName:\t\"$Method$\",\n");
  printer->Print(*vars, "\t\t\tHandler:\t_$Service$_$Method$_Handler,\n");
  printer->Print("\t\t},\n");
}

void PrintServer(google::protobuf::io::Printer* printer,
                 const google::protobuf::ServiceDescriptor* service,
                 map<string, string>* vars) {
  (*vars)["Service"] = service->name();
  printer->Print(*vars, "type $Service$Server interface {\n");
  for (int i = 0; i < service->method_count(); ++i) {
    PrintServerMethodDef(printer, service->method(i), vars);
  }
  printer->Print("}\n\n");

  printer->Print(*vars,
                 "func RegisterService(s *rpc.Server, srv $Service$Server) {\n"
                 "\ts.RegisterService(&_$Service$_serviceDesc, srv)\n"
                 "}\n\n");

  for (int i = 0; i < service->method_count(); ++i) {
    PrintServerHandler(printer, service->method(i), vars);
  }

  printer->Print(*vars,
                 "var _$Service$_serviceDesc = rpc.ServiceDesc{\n"
                 "\tServiceName: \"$Package$$Service$\",\n"
                 "\tHandlerType: (*$Service$Server)(nil),\n"
                 "\tMethods: []rpc.MethodDesc{\n");
  for (int i = 0; i < service->method_count(); ++i) {
    if (NoStreaming(service->method(i))) {
      PrintServerMethodDesc(printer, service->method(i), vars);
    }
  }
  printer->Print("\t},\n");

  printer->Print("\tStreams: []rpc.StreamDesc{\n");
  for (int i = 0; i < service->method_count(); ++i) {
    if (!NoStreaming(service->method(i))) {
      PrintServerStreamingMethodDesc(printer, service->method(i), vars);
    }
  }
  printer->Print(
      "\t},\n"
      "}\n\n");
}

bool IsSelfImport(const google::protobuf::FileDescriptor* self,
                  const google::protobuf::FileDescriptor* import) {
  if (GenerateFullPackage(self) == GenerateFullPackage(import)) {
    return true;
  }
  return false;
}

void PrintMessageImports(
    google::protobuf::io::Printer* printer,
    const google::protobuf::FileDescriptor* file,
    map<string, string>* vars) {
  set<const google::protobuf::FileDescriptor*> descs;
  for (int i = 0; i < file->service_count(); ++i) {
    const google::protobuf::ServiceDescriptor* service = file->service(i);
    for (int j = 0; j < service->method_count(); ++j) {
      const google::protobuf::MethodDescriptor* method = service->method(j);
      if (!IsSelfImport(file, method->input_type()->file())) {
        descs.insert(method->input_type()->file());
      }
      if (!IsSelfImport(file, method->output_type()->file())) {
        descs.insert(method->output_type()->file());
      }
    }
  }

  int idx = 0;
  for (auto fd : descs) {
    string pkg = GenerateFullPackage(fd);
    if (pkg != "") {
      g_imports.insert(pkg);
      if (file->package() == fd->package()) {
        // the same package name in different directories. Require an alias.
        g_import_alias[pkg] = "apb" + std::to_string(idx++);
      }
    }
  }
  for (auto import : g_imports) {
    string import_path = "import ";
    if (g_import_alias.find(import) != g_import_alias.end()) {
      import_path += g_import_alias[import] + " ";
    }
    import_path += "\"" + import + "\"";
    printer->Print(import_path.c_str());
    printer->Print("\n");
  }
  printer->Print("\n");
}

string GetServices(const google::protobuf::FileDescriptor* file) {
  string output;
  google::protobuf::io::StringOutputStream output_stream(&output);
  google::protobuf::io::Printer printer(&output_stream, '$');
  map<string, string> vars;

  string package_name = !file->options().go_package().empty()
                            ? file->options().go_package()
                            : file->package();
  vars["PackageName"] = BadToUnderscore(package_name);
  printer.Print(vars, "package $PackageName$\n\n");
  printer.Print("import (\n");
  if (HasClientOnlyStreaming(file)) {
    printer.Print(
        "\t\"fmt\"\n"
        "\t\"io\"\n");
  }
  printer.Print(
      "\t\"github.com/google/grpc-go/rpc\"\n"
      "\tcontext \"golang.org/x/net/context\"\n"
      "\tproto \"github.com/golang/protobuf/proto\"\n"
      ")\n\n");

  PrintMessageImports(&printer, file, &vars);

  // $Package$ is used to fully qualify method names.
  vars["Package"] = file->package();
  if (!file->package().empty()) {
    vars["Package"].append(".");
  }

  for (int i = 0; i < file->service_count(); ++i) {
    PrintClient(&printer, file->service(0), &vars);
    printer.Print("\n");
    PrintServer(&printer, file->service(0), &vars);
    printer.Print("\n");
  }
  return output;
}

}  // namespace grpc_go_generator

