"""PluginManager SDK generator.

The descriptor format is deliberately JSON-only for the first generator
milestone. JSON keeps the host tool dependency-free and gives deterministic
input/output suitable for CI and release builds.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import struct
import zlib
from pathlib import Path
from typing import Any

SCHEMA = "plugin-manager/plugin-type-1"
DEFAULT_SUFFIX = ".pmp"
UUID_RE = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")
C_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SCALAR_TYPES = {
    "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64",
    "uint64", "size", "bool", "void",
}


class DescriptorError(ValueError):
    """A user-facing descriptor validation error."""


def _required(mapping: dict[str, Any], key: str, path: str) -> Any:
    if key not in mapping:
        raise DescriptorError(f"{path}: missing required field '{key}'")
    return mapping[key]


def _mapping(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DescriptorError(f"{path}: expected an object")
    return value


def _string(value: Any, path: str) -> str:
    if not isinstance(value, str) or not value:
        raise DescriptorError(f"{path}: expected a non-empty string")
    return value


def _identifier(value: Any, path: str) -> str:
    value = _string(value, path)
    if not C_IDENTIFIER_RE.fullmatch(value):
        raise DescriptorError(f"{path}: '{value}' is not a C identifier")
    return value


def _positive_int(value: Any, path: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise DescriptorError(f"{path}: expected a positive integer")
    return value


def _uuid(value: Any, path: str) -> str:
    value = _string(value, path).lower()
    if not UUID_RE.fullmatch(value):
        raise DescriptorError(f"{path}: expected a canonical UUID")
    return value


def _type(value: Any, path: str) -> str:
    value = _string(value, path)
    if value in SCALAR_TYPES or re.fullmatch(r"pointer\((?:const )?[A-Za-z_][A-Za-z0-9_]*\)", value):
        return value
    raise DescriptorError(f"{path}: unsupported ABI type '{value}'")


def _methods(items: Any, path: str) -> list[dict[str, Any]]:
    if not isinstance(items, list):
        raise DescriptorError(f"{path}: expected an array")
    result = []
    names = set()
    for index, raw in enumerate(items):
        item = _mapping(raw, f"{path}[{index}]")
        name = _identifier(_required(item, "name", f"{path}[{index}]"), f"{path}[{index}].name")
        if name in names:
            raise DescriptorError(f"{path}: duplicate method '{name}'")
        names.add(name)
        parameters = item.get("parameters", [])
        if not isinstance(parameters, list):
            raise DescriptorError(f"{path}[{index}].parameters: expected an array")
        normalized_parameters = []
        parameter_names = set()
        for pindex, raw_parameter in enumerate(parameters):
            parameter = _mapping(raw_parameter, f"{path}[{index}].parameters[{pindex}]")
            pname = _identifier(_required(parameter, "name", f"{path}[{index}].parameters[{pindex}]"),
                                f"{path}[{index}].parameters[{pindex}].name")
            if pname in parameter_names:
                raise DescriptorError(f"{path}[{index}]: duplicate parameter '{pname}'")
            parameter_names.add(pname)
            normalized_parameter = {
                "name": pname,
                "type": _type(_required(parameter, "type", f"{path}[{index}].parameters[{pindex}]"),
                               f"{path}[{index}].parameters[{pindex}].type"),
            }
            for attribute in ("direction", "length", "capacity"):
                if attribute in parameter:
                    normalized_parameter[attribute] = _string(parameter[attribute],
                                                              f"{path}[{index}].parameters[{pindex}].{attribute}")
            normalized_parameters.append(normalized_parameter)
        normalized_method = {
            "name": name,
            "return": _type(_required(item, "return", f"{path}[{index}]"), f"{path}[{index}].return"),
            "parameters": normalized_parameters,
        }
        for attribute in ("default_service", "default_method"):
            if attribute in item:
                normalized_method[attribute] = _identifier(item[attribute], f"{path}[{index}].{attribute}")
        if ("default_service" in normalized_method) != ("default_method" in normalized_method):
            raise DescriptorError(f"{path}[{index}]: default_service and default_method must be paired")
        result.append(normalized_method)
    return result


def validate_descriptor(raw: Any) -> dict[str, Any]:
    root = _mapping(raw, "descriptor")
    if _required(root, "schema", "descriptor") != SCHEMA:
        raise DescriptorError(f"descriptor.schema: expected '{SCHEMA}'")
    framework = _mapping(_required(root, "framework", "descriptor"), "framework")
    framework_normalized = {
        "abi_version": _positive_int(_required(framework, "abi_version", "framework"), "framework.abi_version"),
        "package_format": _positive_int(_required(framework, "package_format", "framework"), "framework.package_format"),
        "package_suffix": framework.get("package_suffix", DEFAULT_SUFFIX),
    }
    if framework_normalized["package_suffix"] != DEFAULT_SUFFIX:
        raise DescriptorError("framework.package_suffix: only '.pmp' is supported")
    if framework_normalized["abi_version"] != 1:
        raise DescriptorError("framework.abi_version: only version 1 is supported")
    if framework_normalized["package_format"] != 1:
        raise DescriptorError("framework.package_format: only version 1 is supported")

    application = _mapping(_required(root, "application", "descriptor"), "application")
    plugin_type = _mapping(_required(root, "plugin_type", "descriptor"), "plugin_type")
    target = _mapping(_required(root, "target", "descriptor"), "target")
    limits = _mapping(_required(root, "limits", "descriptor"), "limits")
    normalized = {
        "schema": SCHEMA,
        "framework": framework_normalized,
        "application": {
            "id": _uuid(_required(application, "id", "application"), "application.id"),
            "version": _positive_int(_required(application, "version", "application"), "application.version"),
            "name": _string(_required(application, "name", "application"), "application.name"),
        },
        "plugin_type": {
            "id": _uuid(_required(plugin_type, "id", "plugin_type"), "plugin_type.id"),
            "name": _string(_required(plugin_type, "name", "plugin_type"), "plugin_type.name"),
            "version": _positive_int(_required(plugin_type, "version", "plugin_type"), "plugin_type.version"),
        },
        "target": {
            "architecture": _string(_required(target, "architecture", "target"), "target.architecture"),
            "cpu": _string(_required(target, "cpu", "target"), "target.cpu"),
            "abi": _string(_required(target, "abi", "target"), "target.abi"),
        },
        "interfaces": [],
        "services": [],
        "limits": {},
    }
    if normalized["target"] != {
            "architecture": "arm-cortex-m", "cpu": "cortex-m0plus", "abi": "aapcs"}:
        raise DescriptorError(
            "target: only arm-cortex-m/cortex-m0plus/aapcs is currently supported")
    for field in ("name_max", "version_max", "max_image_size", "max_ram_size", "max_stack_size"):
        normalized["limits"][field] = _positive_int(_required(limits, field, "limits"), f"limits.{field}")

    ids: set[str] = set()
    interfaces = root.get("interfaces", [])
    if not isinstance(interfaces, list):
        raise DescriptorError("interfaces: expected an array")
    if not interfaces:
        raise DescriptorError("interfaces: at least one interface is required")
    for index, raw_interface in enumerate(interfaces):
        interface = _mapping(raw_interface, f"interfaces[{index}]")
        interface_id = _uuid(_required(interface, "id", f"interfaces[{index}]"), f"interfaces[{index}].id")
        if interface_id in ids:
            raise DescriptorError(f"interfaces[{index}].id: duplicate UUID")
        ids.add(interface_id)
        normalized["interfaces"].append({
            "id": interface_id,
            "name": _identifier(_required(interface, "name", f"interfaces[{index}]"), f"interfaces[{index}].name"),
            "version": _positive_int(_required(interface, "version", f"interfaces[{index}]"), f"interfaces[{index}].version"),
            "instance_type": _identifier(_required(interface, "instance_type", f"interfaces[{index}]"), f"interfaces[{index}].instance_type"),
            "methods": _methods(_required(interface, "methods", f"interfaces[{index}]"), f"interfaces[{index}].methods"),
        })

    services = root.get("services", [])
    if not isinstance(services, list):
        raise DescriptorError("services: expected an array")
    for index, raw_service in enumerate(services):
        service = _mapping(raw_service, f"services[{index}]")
        service_id = _uuid(_required(service, "id", f"services[{index}]"), f"services[{index}].id")
        if service_id in ids:
            raise DescriptorError(f"services[{index}].id: duplicate UUID")
        ids.add(service_id)
        normalized["services"].append({
            "id": service_id,
            "name": _identifier(_required(service, "name", f"services[{index}]"), f"services[{index}].name"),
            "version": _positive_int(_required(service, "version", f"services[{index}]"), f"services[{index}].version"),
            "vtable": _identifier(_required(service, "vtable", f"services[{index}]"), f"services[{index}].vtable"),
            "methods": _methods(_required(service, "methods", f"services[{index}]"), f"services[{index}].methods"),
        })
    service_methods = {
        service["name"]: {method["name"]: method for method in service["methods"]}
        for service in normalized["services"]
    }
    for interface in normalized["interfaces"]:
        for method in interface["methods"]:
            if "default_service" in method:
                service_name = method["default_service"]
                service_method_name = method["default_method"]
                if service_name not in service_methods or \
                        service_method_name not in service_methods[service_name]:
                    raise DescriptorError(
                        f"{interface['name']}.{method['name']}: default service method is not declared")
                service_method = service_methods[service_name][service_method_name]
                interface_parameters = method["parameters"]
                if interface_parameters and interface_parameters[0]["name"] == "instance":
                    interface_parameters = interface_parameters[1:]
                interface_signature = [
                    (parameter["name"], parameter["type"])
                    for parameter in interface_parameters
                ]
                service_signature = [
                    (parameter["name"], parameter["type"])
                    for parameter in service_method["parameters"]
                ]
                if method["return"] != service_method["return"] or \
                        interface_signature != service_signature:
                    raise DescriptorError(
                        f"{interface['name']}.{method['name']}: default service method has an incompatible signature")
    required_services = root.get("required_services", [])
    if not isinstance(required_services, list):
        raise DescriptorError("required_services: expected an array")
    normalized_required_services = []
    service_ids = {service["id"] for service in normalized["services"]}
    for index, required in enumerate(required_services):
        required_id = _uuid(required, f"required_services[{index}]")
        if required_id not in service_ids:
            raise DescriptorError(
                f"required_services[{index}]: UUID is not declared in services")
        if required_id in normalized_required_services:
            raise DescriptorError(f"required_services[{index}]: duplicate UUID")
        normalized_required_services.append(required_id)
    if len(normalized_required_services) > 64:
        raise DescriptorError("required_services: maximum is 64")
    normalized["required_services"] = normalized_required_services
    return normalized


def load_descriptor(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DescriptorError(f"{path}: cannot read JSON descriptor: {exc}") from exc
    return validate_descriptor(raw)


def _c_type(type_name: str) -> str:
    scalars = {
        "int8": "int8_t", "uint8": "uint8_t", "int16": "int16_t", "uint16": "uint16_t",
        "int32": "int32_t", "uint32": "uint32_t", "int64": "int64_t", "uint64": "uint64_t",
        "size": "size_t", "bool": "bool", "void": "void",
    }
    if type_name in scalars:
        return scalars[type_name]
    if type_name.startswith("pointer(const "):
        return f"const {_c_type(type_name[14:-1])} *"
    if type_name.startswith("pointer("):
        return f"{_c_type(type_name[8:-1])} *"
    return f"struct {type_name}"


def _method_declaration(method: dict[str, Any], prefix: str) -> str:
    parameters = [f"{_c_type(p['type'])} {p['name']}" for p in method["parameters"]]
    return f"    {_c_type(method['return'])} (*{method['name']})({', '.join(parameters) or 'void'});"


def generate_sdk(descriptor: dict[str, Any], output: Path) -> None:
    files: dict[str, str] = {}
    files["sdk-manifest.json"] = json.dumps(descriptor, indent=2, sort_keys=True) + "\n"
    files["include/pm_sdk/pm_sdk.h"] = """#ifndef PM_SDK_H\n#define PM_SDK_H\n\n#include <stddef.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include \"plugin_manager/plugin_abi.h\"\n\n#endif\n"""
    referenced_types: set[str] = set()
    for group in descriptor["interfaces"] + descriptor["services"]:
        for method in group["methods"]:
            for parameter in method["parameters"]:
                parameter_type = parameter["type"]
                if parameter_type.startswith("pointer("):
                    referenced_types.add(parameter_type[8:-1].removeprefix("const "))
    scalar_names = set(SCALAR_TYPES)
    scalar_names.add("void")
    forward_declarations = sorted(
        type_name for type_name in referenced_types
        if type_name not in scalar_names and C_IDENTIFIER_RE.fullmatch(type_name)
    )
    types = ["#ifndef PM_SDK_GENERATED_TYPES_H", "#define PM_SDK_GENERATED_TYPES_H", "", "#include <stdbool.h>", "#include <stddef.h>", "#include <stdint.h>", ""]
    types.extend(f"struct {type_name};" for type_name in forward_declarations)
    if forward_declarations:
        types.append("")
    interfaces = ["#ifndef PM_SDK_GENERATED_INTERFACES_H", "#define PM_SDK_GENERATED_INTERFACES_H", "", '#include "pm_sdk/generated_types.h"', ""]
    services = ["#ifndef PM_SDK_GENERATED_SERVICES_H", "#define PM_SDK_GENERATED_SERVICES_H", "", '#include "pm_sdk/generated_types.h"', ""]
    for interface in descriptor["interfaces"]:
        interfaces.extend([f"struct {interface['name']}_api {{"])
        interfaces.extend(_method_declaration(method, interface["name"]) for method in interface["methods"])
        interfaces.extend(["};", ""])
    for service in descriptor["services"]:
        services.extend([f"struct {service['vtable']} {{"])
        services.extend(_method_declaration(method, service["name"]) for method in service["methods"])
        services.extend(["};", ""])
    interfaces.extend(["#endif", ""])
    services.extend(["#endif", ""])
    types.extend(["#endif", ""])
    files["include/pm_sdk/generated_types.h"] = "\n".join(types)
    files["include/pm_sdk/generated_interfaces.h"] = "\n".join(interfaces)
    files["include/pm_sdk/generated_services.h"] = "\n".join(services)
    files["include/pm_sdk/generated_ids.h"] = _ids_header(descriptor)
    interface = descriptor["interfaces"][0] if descriptor["interfaces"] else None
    if interface is not None:
        files["include/pm_sdk/generated_runtime.h"] = _runtime_header(interface, descriptor["services"])
        files["src/plugin_entry.c"] = _runtime_source(interface, descriptor["services"])
    else:
        files["include/pm_sdk/generated_runtime.h"] = _runtime_header(None, descriptor["services"])
        files["src/plugin_entry.c"] = _runtime_source(None, descriptor["services"])
    if interface is not None:
        application_bytes = _uuid_bytes(descriptor["application"]["id"])
        interface_bytes = _uuid_bytes(interface["id"])
        files["src/plugin_descriptor.c"] = _descriptor_source(
            application_bytes, interface_bytes, interface["version"],
            descriptor["required_services"])
    files["plugin.c"] = """#include \"plugin_manager/plugin_abi.h\"\n#include \"pm_sdk/generated_interfaces.h\"\n#include \"pm_sdk/generated_services.h\"\n#include \"pm_sdk/generated_runtime.h\"\n\n/* Add application-specific method logic here, replacing the generated default\n * method implementations when the plugin type requires behavior. */\n"""
    files["tests/runtime_harness.c"] = _runtime_harness_source(descriptor)
    files["CMakeLists.txt"] = """cmake_minimum_required(VERSION 3.20)

project(generated_plugin C)
enable_testing()

set(PM_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "Generated PluginManager SDK")
set(PLUGIN_MANAGER_DIR "" CACHE PATH "PluginManager source directory")
set(PM_ARM_TOOLCHAIN_PREFIX "" CACHE STRING "ARM compiler prefix")
set(PM_PLUGIN_ID 0 CACHE STRING "Plugin ID")
set(PM_PLUGIN_NAME "" CACHE STRING "Plugin name")
set(PM_PLUGIN_VERSION "" CACHE STRING "Plugin version")
if(PM_ARM_TOOLCHAIN_PREFIX)
    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_C_COMPILER "${PM_ARM_TOOLCHAIN_PREFIX}-gcc" CACHE FILEPATH "ARM compiler" FORCE)
endif()

set(PM_PLUGIN_TEXT_ORIGIN 0x00000000 CACHE STRING "Plugin text origin")
set(PM_PLUGIN_TEXT_LENGTH 0x00002000 CACHE STRING "Plugin text length")
set(PM_PLUGIN_RODATA_ORIGIN 0x00002000 CACHE STRING "Plugin rodata origin")
set(PM_PLUGIN_RODATA_LENGTH 0x00002000 CACHE STRING "Plugin rodata length")
set(PM_PLUGIN_DATA_ORIGIN 0x20010000 CACHE STRING "Plugin data origin")
set(PM_PLUGIN_DATA_LENGTH 0x00002000 CACHE STRING "Plugin data length")
configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_memory.ld.in
               ${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld @ONLY)

if(PM_ARM_TOOLCHAIN_PREFIX)
    add_executable(plugin plugin.c src/plugin_entry.c src/plugin_descriptor.c)
    target_include_directories(plugin PRIVATE include "${PM_SDK_ROOT}/include" "${PLUGIN_MANAGER_DIR}/include")
    target_compile_definitions(plugin PRIVATE PM_PLUGIN_ID=${PM_PLUGIN_ID}
        PM_PLUGIN_NAME=\\\"${PM_PLUGIN_NAME}\\\"
        PM_PLUGIN_VERSION=\\\"${PM_PLUGIN_VERSION}\\\")
    target_compile_options(plugin PRIVATE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
        -ffreestanding -fno-builtin -fno-common -ffunction-sections -fdata-sections
        -Wall -Wextra)
    target_link_options(plugin PRIVATE -r -nostdlib -nostartfiles -nodefaultlibs
        -T${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld -Wl,--emit-relocs)
    set_target_properties(plugin PROPERTIES OUTPUT_NAME "plugin" SUFFIX ".elf"
        LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld")
    add_custom_target(package_hint DEPENDS plugin)
endif()

add_executable(runtime_harness tests/runtime_harness.c src/plugin_entry.c
    "${PLUGIN_MANAGER_DIR}/src/plugin_registry.c")
target_include_directories(runtime_harness PRIVATE include "${PM_SDK_ROOT}/include" "${PLUGIN_MANAGER_DIR}/include")
target_compile_features(runtime_harness PRIVATE c_std_11)
add_test(NAME generated_runtime_harness COMMAND runtime_harness)
"""
    files["cmake/plugin_memory.ld.in"] = """MEMORY
{
    PM_PLUGIN_TEXT (rx) : ORIGIN = @PM_PLUGIN_TEXT_ORIGIN@, LENGTH = @PM_PLUGIN_TEXT_LENGTH@
    PM_PLUGIN_RODATA (r) : ORIGIN = @PM_PLUGIN_RODATA_ORIGIN@, LENGTH = @PM_PLUGIN_RODATA_LENGTH@
    PM_PLUGIN_DATA (rw) : ORIGIN = @PM_PLUGIN_DATA_ORIGIN@, LENGTH = @PM_PLUGIN_DATA_LENGTH@
}
INCLUDE "@PLUGIN_MANAGER_DIR@/cmake/plugin_manager_plugin.ld"
ENTRY(pm_plugin_get_descriptor)
"""
    output.mkdir(parents=True, exist_ok=True)
    for relative, content in sorted(files.items()):
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8", newline="\n")


def _uuid_bytes(value: str) -> bytes:
    return bytes.fromhex(value.replace("-", ""))


def _c_bytes(value: bytes) -> str:
    return ", ".join(f"0x{byte:02x}u" for byte in value)


def _ids_header(descriptor: dict[str, Any]) -> str:
    lines = [
        "#ifndef PM_SDK_GENERATED_IDS_H", "#define PM_SDK_GENERATED_IDS_H", "",
        '#include "plugin_manager/plugin_abi.h"', "",
        f"#define PM_SDK_APPLICATION_VERSION {descriptor['application']['version']}u",
    ]
    lines.append(
        f"static const pm_uuid_t PM_SDK_APPLICATION_ID = {{{{{_c_bytes(_uuid_bytes(descriptor['application']['id']))}}}}};")
    for group_name, groups in (("INTERFACE", descriptor["interfaces"]), ("SERVICE", descriptor["services"])):
        for group in groups:
            macro = re.sub(r"[^A-Za-z0-9]", "_", group["name"]).upper()
            lines.append(
                f"static const pm_uuid_t PM_SDK_{group_name}_{macro}_ID = "
                f"{{{{{_c_bytes(_uuid_bytes(group['id']))}}}}};")
            lines.append(f"#define PM_SDK_{group_name}_{macro}_VERSION {group['version']}u")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def _runtime_harness_source(descriptor: dict[str, Any]) -> str:
    interface = descriptor["interfaces"][0]
    delegated = next((method for method in interface["methods"]
                      if "default_service" in method), None)
    if delegated is None:
        return """#include <assert.h>\nint main(void) { return 0; }\n"""
    service = next(service for service in descriptor["services"]
                   if service["name"] == delegated["default_service"])
    method = next(method for method in service["methods"]
                  if method["name"] == delegated["default_method"])
    parameters = delegated["parameters"]
    call_parameters = [parameter["name"] for parameter in parameters
                       if parameter["name"] != "instance"]
    service_parameters = [parameter["name"] for parameter in method["parameters"]]
    argument_declarations = []
    for parameter in parameters:
        if parameter["name"] == "instance":
            continue
        argument_declarations.append(f"    {_c_type(parameter['type'])} {parameter['name']} = {{0}};")
    expected = "1"
    return f'''#include <assert.h>
#include "plugin_manager/plugin_abi.h"
#include "pm_sdk/generated_ids.h"
#include "pm_sdk/generated_interfaces.h"
#include "pm_sdk/generated_services.h"
 #include "pm_sdk/generated_runtime.h"

int pm_plugin_create(const struct pm_service_table *services, void **instance);
int pm_plugin_destroy(void *instance);

static int service_{method['name']}({', '.join(_c_type(parameter['type']) + ' ' + parameter['name'] for parameter in method['parameters'])})
{{
    (void)0;
    return {expected};
}}

int main(void)
{{
    static const struct {service['vtable']} service = {{
        .{method['name']} = service_{method['name']},
    }};
    static const struct pm_service services[] = {{
        {{.service_id = PM_SDK_SERVICE_{re.sub(r"[^A-Za-z0-9]", "_", service['name']).upper()}_ID,
          .version = PM_SDK_SERVICE_{re.sub(r"[^A-Za-z0-9]", "_", service['name']).upper()}_VERSION,
          .vtable = &service}},
    }};
    const struct pm_service_table table = {{
        .count = 1u, .items = services,
    }};
    void *instance = NULL;
{chr(10).join(argument_declarations)}
    assert(pm_plugin_create(&table, &instance) == PM_OK);
    assert(instance != NULL);
    const struct {interface['name']}_api *api = pm_sdk_interface_get(instance);
    assert(api != NULL);
    assert(api->{delegated['name']}(instance, {', '.join(call_parameters)}) == {expected});
    assert(pm_plugin_destroy(instance) == PM_OK);
    return 0;
}}
'''


def _runtime_header(interface: dict[str, Any] | None,
                    services: list[dict[str, Any]] | None = None) -> str:
    services = services or []
    api = f"    struct {interface['name']}_api api;\n" if interface else ""
    accessors = []
    for service in services:
        accessors.append(
            f"const struct {service['vtable']} *pm_sdk_{service['name']}_get("
            "const struct pm_service_table *services);")
    return f'''#ifndef PM_SDK_GENERATED_RUNTIME_H
#define PM_SDK_GENERATED_RUNTIME_H

#include <stddef.h>
#include "plugin_manager/plugin_abi.h"
#include "pm_sdk/generated_interfaces.h"
#include "pm_sdk/generated_services.h"
#include "pm_sdk/generated_ids.h"

struct pm_generated_plugin_instance {{
    const struct pm_service_table *services;
    size_t service_count;
{api}}};

const struct pm_service *pm_sdk_service_find(
    const struct pm_service_table *services, const pm_uuid_t *service_id);

{chr(10).join(accessors)}

void *pm_sdk_plugin_instance(void);

{f"const struct {interface['name']}_api *pm_sdk_interface_get(void *instance);" if interface else ""}

#endif
'''


def _runtime_source(interface: dict[str, Any] | None,
                    services: list[dict[str, Any]] | None = None) -> str:
    services = services or []
    methods = []
    initializers = []
    if interface:
        service_by_name = {service["name"]: service for service in services}
        for method in interface["methods"]:
            parameters = [f"{_c_type(p['type'])} {p['name']}" for p in method["parameters"]]
            return_type = _c_type(method["return"])
            if "default_service" in method:
                service = service_by_name[method["default_service"]]
                args = [parameter["name"] for parameter in method["parameters"]]
                if args and method["parameters"][0]["name"] == "instance":
                    args = args[1:]
                call = f"service->{method['default_method']}({', '.join(args)})"
                body = f"const struct {service['vtable']} *service = pm_sdk_{service['name']}_get(pm_instance.services); if (service == NULL) {{ return PM_ENOENT; }} return {call};"
            else:
                body = "return PM_ENOTSUP;" if return_type != "void" else "return;"
            methods.append(
                f"static {return_type} pm_sdk_default_{interface['name']}_{method['name']}"
                f"({', '.join(parameters) or 'void'}) {{ (void)0; {body} }}")
            initializers.append(f"        .{method['name']} = pm_sdk_default_{interface['name']}_{method['name']},")
        api_init = f"    .api = {{\n" + "\n".join(initializers) + "\n    },"
    else:
        api_init = ""
    return f'''#include <stddef.h>
#include "plugin_manager/plugin_abi.h"
#include "pm_sdk/generated_runtime.h"
#include "pm_sdk/generated_ids.h"

static struct pm_generated_plugin_instance pm_instance;

{chr(10).join(methods)}

 static struct pm_generated_plugin_instance pm_instance = {{
    .services = NULL,
    .service_count = 0u,
{api_init}
}};

const struct pm_service *pm_sdk_service_find(
    const struct pm_service_table *services, const pm_uuid_t *service_id)
{{
    size_t index;
    if (services == NULL || service_id == NULL) {{
 return NULL;
}}

    for (index = 0u; index < services->count; ++index) {{
        if (pm_uuid_equal(&services->items[index].service_id, service_id)) {{
            return &services->items[index];
        }}
    }}
    return NULL;
}}

{chr(10).join(_service_accessor_source(service) for service in services)}

void *pm_sdk_plugin_instance(void)
{{
    return &pm_instance;
}}

{f'''const struct {interface['name']}_api *pm_sdk_interface_get(void *instance)
{{
    struct pm_generated_plugin_instance *value = instance;
    return value == &pm_instance ? &value->api : NULL;
}}
''' if interface else ''}

int pm_plugin_create(const struct pm_service_table *services, void **instance)
{{
    if (services == NULL || instance == NULL) {{
        return PM_EINVAL;
    }}
    pm_instance.services = services;
    pm_instance.service_count = services->count;
    *instance = &pm_instance;
    return PM_OK;
}}

int pm_plugin_destroy(void *instance)
{{
    if (instance != &pm_instance) {{
        return PM_EINVAL;
    }}
    pm_instance.services = NULL;
    pm_instance.service_count = 0u;
    return PM_OK;
}}
'''


def _service_accessor_source(service: dict[str, Any]) -> str:
    return f'''const struct {service['vtable']} *pm_sdk_{service['name']}_get(
    const struct pm_service_table *services)
{{
    const struct pm_service *service =
        pm_sdk_service_find(services, &PM_SDK_SERVICE_{re.sub(r"[^A-Za-z0-9]", "_", service['name']).upper()}_ID);
    return service == NULL ? NULL :
        (const struct {service['vtable']} *)service->vtable;
}}
'''


def _descriptor_source(application_id: bytes, interface_id: bytes,
                       interface_version: int,
                       required_services: list[str]) -> str:
    required_count = len(required_services)
    required_offset = 80 if required_count else 0
    # Keep the generated object layout fixed; the wire offsets point at the
    # populated prefix of the reserved service-record area.
    name_offset = 80 + 64 * 16
    return f'''#include <stdint.h>
#include <stddef.h>
#include "plugin_manager/plugin_descriptor.h"

#ifndef PM_PLUGIN_ID
#define PM_PLUGIN_ID 0u
#endif
#ifndef PM_PLUGIN_NAME
#define PM_PLUGIN_NAME ""
#endif
#ifndef PM_PLUGIN_VERSION
#define PM_PLUGIN_VERSION ""
#endif

extern int pm_plugin_create(const struct pm_service_table *services, void **instance);
extern int pm_plugin_destroy(void *instance);

struct pm_generated_wire_descriptor {{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t application_id[16];
    uint8_t interface_id[16];
    uint32_t interface_version;
    uint32_t plugin_id;
    uint32_t required_count;
    uint32_t required_offset;
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t version_offset;
    uint32_t version_size;
    uint32_t create_address;
    uint32_t destroy_address;
    uint8_t required_services[64][16];
    char name[64];
    char plugin_version[32];
}};

__attribute__((section(".rodata.pm_descriptor"), used))
const struct pm_generated_wire_descriptor pm_plugin_get_descriptor = {{
    .magic = PM_WIRE_DESCRIPTOR_MAGIC,
    .version = PM_WIRE_DESCRIPTOR_VERSION,
    .size = PM_WIRE_DESCRIPTOR_HEADER_SIZE,
    .application_id = {{{_c_bytes(application_id)}}},
    .interface_id = {{{_c_bytes(interface_id)}}},
    .interface_version = {interface_version}u,
    .plugin_id = PM_PLUGIN_ID,
    .required_count = {required_count}u,
    .required_offset = {required_offset}u,
    .name_offset = {name_offset}u,
    .name_size = sizeof(PM_PLUGIN_NAME),
    .version_offset = {name_offset}u + 64u,
    .version_size = sizeof(PM_PLUGIN_VERSION),
    .create_address = (uint32_t)(uintptr_t)pm_plugin_create,
    .destroy_address = (uint32_t)(uintptr_t)pm_plugin_destroy,
    .required_services = {{
        {', '.join('{' + _c_bytes(_uuid_bytes(value)) + '}' for value in required_services)}
    }},
    .name = PM_PLUGIN_NAME,
    .plugin_version = PM_PLUGIN_VERSION
}};
'''


def build_package(image: bytes, application_id: bytes, interface_id: bytes,
                  interface_version: int, plugin_id: int, name: str,
                  version: str, ram_size: int, stack_size: int,
                  required_services: list[bytes] | None = None) -> bytes:
    required_services = required_services or []
    name_bytes = name.encode("ascii") + b"\0"
    version_bytes = version.encode("ascii") + b"\0"
    name_offset = 108
    version_offset = name_offset + len(name_bytes)
    service_offset = (version_offset + len(version_bytes) + 3) & ~3
    image_offset = (service_offset + len(required_services) * 16 + 3) & ~3
    total_size = image_offset + len(image)
    package = bytearray(total_size)
    struct.pack_into("<IHHI", package, 0, 0x504D504B, 1, 108, total_size)
    package[12:28] = application_id
    package[28:44] = interface_id
    struct.pack_into("<IIHHIIIIIIIIIII", package, 44, interface_version,
                     plugin_id, 1, 0, image_offset, len(image), ram_size,
                     stack_size, zlib.crc32(image) & 0xffffffff, name_offset,
                     len(name_bytes), version_offset, len(version_bytes),
                     service_offset, len(required_services))
    package[name_offset:name_offset + len(name_bytes)] = name_bytes
    package[version_offset:version_offset + len(version_bytes)] = version_bytes
    for index, service_id in enumerate(required_services):
        package[service_offset + index * 16:service_offset + (index + 1) * 16] = service_id
    package[image_offset:] = image
    return bytes(package)


def package_elf(descriptor: dict[str, Any], elf: Path, output: Path,
                plugin_id: int, name: str, version: str) -> None:
    image = elf.read_bytes()
    if not image:
        raise DescriptorError(f"{elf}: ELF file is empty")
    if len(image) > descriptor["limits"]["max_image_size"]:
        raise DescriptorError(f"{elf}: image exceeds max_image_size")
    if not descriptor["interfaces"]:
        raise DescriptorError("descriptor.interfaces: at least one interface is required")
    interface = descriptor["interfaces"][0]
    if len(name) > descriptor["limits"]["name_max"] or len(version) > descriptor["limits"]["version_max"]:
        raise DescriptorError("plugin name or version exceeds descriptor limits")
    package = build_package(
        image, _uuid_bytes(descriptor["application"]["id"]),
        _uuid_bytes(interface["id"]), interface["version"], plugin_id,
        name, version, descriptor["limits"]["max_ram_size"],
        descriptor["limits"]["max_stack_size"],
        [_uuid_bytes(value) for value in descriptor["required_services"]])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="plugin-sdk-builder")
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("validate", "inspect", "generate"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--descriptor", "--contract", required=True, type=Path)
        if command == "generate":
            subparser.add_argument("--output", required=True, type=Path)
    package_parser = subparsers.add_parser("package")
    package_parser.add_argument("--descriptor", "--contract", required=True, type=Path)
    package_parser.add_argument("--elf", required=True, type=Path)
    package_parser.add_argument("--output", required=True, type=Path)
    package_parser.add_argument("--plugin-id", required=True, type=int)
    package_parser.add_argument("--name", required=True)
    package_parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    try:
        descriptor = load_descriptor(args.descriptor)
        if args.command == "validate":
            print(f"valid: {args.descriptor}")
        elif args.command == "inspect":
            print(json.dumps(descriptor, indent=2, sort_keys=True))
        elif args.command == "generate":
            generate_sdk(descriptor, args.output)
            print(f"generated: {args.output}")
        else:
            package_elf(descriptor, args.elf, args.output, args.plugin_id,
                        args.name, args.version)
            print(f"packaged: {args.output}")
        return 0
    except DescriptorError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
