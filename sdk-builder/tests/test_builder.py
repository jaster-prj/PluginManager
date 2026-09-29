import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))

from plugin_sdk_builder import (DescriptorError, build_package, generate_sdk,
                                load_descriptor, package_elf, validate_descriptor)


VALID = {
    "schema": "plugin-manager/plugin-type-1",
    "framework": {"abi_version": 1, "package_format": 1},
    "application": {
        "id": "00112233-4455-6677-8899-aabbccddeeff",
        "version": 1,
        "name": "Example Application",
    },
    "plugin_type": {
        "id": "11223344-5566-7788-99aa-bbccddeeff00",
        "version": 1,
        "name": "Example Device",
    },
    "target": {"architecture": "arm-cortex-m", "cpu": "cortex-m0plus", "abi": "aapcs"},
    "interfaces": [{
        "id": "22334455-6677-8899-aabb-ccddeeff0011",
        "name": "example_device",
        "version": 1,
        "instance_type": "example_instance",
        "methods": [{
            "name": "measure",
            "return": "int32",
            "parameters": [
                {"name": "instance", "type": "pointer(void)"},
                {"name": "result", "type": "pointer(example_result)", "direction": "out"},
            ],
        }],
    }],
    "services": [],
    "required_services": [],
    "limits": {"name_max": 63, "version_max": 31, "max_image_size": 8192,
                "max_ram_size": 8192, "max_stack_size": 2048},
}


class BuilderTests(unittest.TestCase):
    def test_valid_descriptor_normalizes_default_suffix(self):
        result = validate_descriptor(VALID)
        self.assertEqual(result["framework"]["package_suffix"], ".pmp")
        self.assertEqual(result["application"]["id"], VALID["application"]["id"])

    def test_rejects_duplicate_ids(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["services"] = [{
            "id": descriptor["interfaces"][0]["id"], "name": "service", "version": 1,
            "vtable": "service_table", "methods": [],
        }]
        with self.assertRaisesRegex(DescriptorError, "duplicate UUID"):
            validate_descriptor(descriptor)

    def test_rejects_unsafe_type(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["interfaces"][0]["methods"][0]["return"] = "int"
        with self.assertRaisesRegex(DescriptorError, "unsupported ABI type"):
            validate_descriptor(descriptor)

    def test_rejects_non_pmp_suffix(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["framework"]["package_suffix"] = ".plugin"
        with self.assertRaisesRegex(DescriptorError, "only '.pmp'"):
            validate_descriptor(descriptor)

    def test_rejects_unsupported_framework_or_target(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["framework"]["abi_version"] = 2
        with self.assertRaisesRegex(DescriptorError, "only version 1"):
            validate_descriptor(descriptor)
        descriptor = json.loads(json.dumps(VALID))
        descriptor["target"]["cpu"] = "cortex-m4"
        with self.assertRaisesRegex(DescriptorError, "only arm-cortex-m"):
            validate_descriptor(descriptor)

    def test_requires_an_interface(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["interfaces"] = []
        with self.assertRaisesRegex(DescriptorError, "at least one"):
            validate_descriptor(descriptor)

    def test_rejects_unknown_required_service(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["required_services"] = ["33445566-7788-99aa-bbcc-ddeeff001122"]
        with self.assertRaisesRegex(DescriptorError, "not declared"):
            validate_descriptor(descriptor)

    def test_generation_is_deterministic(self):
        descriptor = validate_descriptor(VALID)
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            generate_sdk(descriptor, Path(first))
            generate_sdk(descriptor, Path(second))
            first_files = sorted(path.relative_to(first) for path in Path(first).rglob("*"))
            second_files = sorted(path.relative_to(second) for path in Path(second).rglob("*"))
            self.assertEqual(first_files, second_files)
            for relative in first_files:
                if (Path(first) / relative).is_file():
                    self.assertEqual((Path(first) / relative).read_bytes(),
                                     (Path(second) / relative).read_bytes())

    def test_generated_headers_contain_contract(self):
        descriptor = validate_descriptor(VALID)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(descriptor, output)
            header = (output / "include/pm_sdk/generated_interfaces.h").read_text()
            self.assertIn("struct example_device_api", header)
            self.assertIn("(*measure)", header)
            types = (output / "include/pm_sdk/generated_types.h").read_text()
            self.assertIn("struct example_result;", types)
            self.assertTrue((output / "CMakeLists.txt").exists())
            cmake = (output / "CMakeLists.txt").read_text()
            self.assertIn("PM_PLUGIN_ID", cmake)
            self.assertIn("--emit-relocs", cmake)
            self.assertTrue((output / "cmake/plugin_memory.ld.in").exists())

    def test_generated_descriptor_source_contains_wire_abi(self):
        descriptor = validate_descriptor(VALID)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(descriptor, output)
            source = (output / "src/plugin_descriptor.c").read_text()
            self.assertIn("PM_WIRE_DESCRIPTOR_MAGIC", source)
            self.assertIn("PM_PLUGIN_ID", source)
            self.assertIn("pm_plugin_get_descriptor", source)

    def test_generated_entry_has_executable_lifecycle(self):
        descriptor = validate_descriptor(VALID)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(descriptor, output)
            entry = (output / "src/plugin_entry.c").read_text()
            self.assertIn("pm_instance.service_count", entry)
            self.assertIn("return PM_OK", entry)
            self.assertIn("pm_plugin_destroy", entry)

    def test_generated_runtime_exposes_service_lookup_and_interface_api(self):
        descriptor = validate_descriptor(VALID)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(descriptor, output)
            runtime = (output / "include/pm_sdk/generated_runtime.h").read_text()
            source = (output / "src/plugin_entry.c").read_text()
            self.assertIn("pm_sdk_service_find", runtime)
            self.assertIn("struct example_device_api api", runtime)
            self.assertIn("pm_sdk_default_example_device_measure", source)

    def test_generated_ids_and_typed_service_accessor(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["services"] = [{
            "id": "33445566-7788-99aa-bbcc-ddeeff001122",
            "name": "transport", "version": 1, "vtable": "transport_api",
            "methods": [],
        }]
        descriptor["required_services"] = [descriptor["services"][0]["id"]]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(validate_descriptor(descriptor), output)
            ids = (output / "include/pm_sdk/generated_ids.h").read_text()
            runtime = (output / "include/pm_sdk/generated_runtime.h").read_text()
            source = (output / "src/plugin_entry.c").read_text()
            self.assertIn("PM_SDK_SERVICE_TRANSPORT_ID", ids)
            self.assertIn("pm_sdk_transport_get", runtime)
            self.assertIn("pm_sdk_transport_get", source)

    def test_generated_interface_can_delegate_to_service(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["services"] = [{
            "id": "33445566-7788-99aa-bbcc-ddeeff001122",
            "name": "transport", "version": 1, "vtable": "transport_api",
            "methods": [{
                "name": "measure", "return": "int32",
                "parameters": [{"name": "result", "type": "pointer(example_result)"}],
            }],
        }]
        descriptor["required_services"] = [descriptor["services"][0]["id"]]
        descriptor["interfaces"][0]["methods"][0]["default_service"] = "transport"
        descriptor["interfaces"][0]["methods"][0]["default_method"] = "measure"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(validate_descriptor(descriptor), output)
            source = (output / "src/plugin_entry.c").read_text()
            self.assertIn("pm_sdk_transport_get", source)
            self.assertIn("service->measure", source)

    def test_rejects_incompatible_service_delegation(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["services"] = [{
            "id": "33445566-7788-99aa-bbcc-ddeeff001122",
            "name": "transport", "version": 1, "vtable": "transport_api",
            "methods": [{"name": "measure", "return": "uint32", "parameters": []}],
        }]
        descriptor["interfaces"][0]["methods"][0]["default_service"] = "transport"
        descriptor["interfaces"][0]["methods"][0]["default_method"] = "measure"
        with self.assertRaisesRegex(DescriptorError, "incompatible signature"):
            validate_descriptor(descriptor)

    def test_generated_descriptor_contains_required_service_records(self):
        descriptor = json.loads(json.dumps(VALID))
        descriptor["services"] = [{
            "id": "33445566-7788-99aa-bbcc-ddeeff001122",
            "name": "transport", "version": 1, "vtable": "transport_api",
            "methods": [],
        }]
        descriptor["required_services"] = [descriptor["services"][0]["id"]]
        normalized = validate_descriptor(descriptor)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(normalized, output)
            source = (output / "src/plugin_descriptor.c").read_text()
            self.assertIn("required_services", source)
            self.assertIn(".required_count = 1u", source)

    def test_package_has_parser_compatible_header(self):
        descriptor = validate_descriptor(VALID)
        package = build_package(
            b"ELF-image", bytes.fromhex("00112233445566778899aabbccddeeff"),
            bytes.fromhex("2233445566778899aabbccddeeff0011"), 1, 42,
            "example-plugin", "1.0", 8192, 2048)
        self.assertEqual(package[:4], struct.pack("<I", 0x504D504B))
        self.assertEqual(len(package), int.from_bytes(package[8:12], "little"))
        self.assertEqual(package[108:123], b"example-plugin\0")

    def test_package_contains_required_service_records(self):
        service_id = bytes.fromhex("33445566778899aabbccddeeff001122")
        package = build_package(
            b"ELF-image", bytes.fromhex("00112233445566778899aabbccddeeff"),
            bytes.fromhex("2233445566778899aabbccddeeff0011"), 1, 42,
            "example-plugin", "1.0", 8192, 2048, [service_id])
        service_offset = int.from_bytes(package[92:96], "little")
        self.assertEqual(int.from_bytes(package[96:100], "little"), 1)
        self.assertEqual(package[service_offset:service_offset + 16], service_id)

    def test_generated_target_build_contract(self):
        descriptor = validate_descriptor(VALID)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_sdk(descriptor, output)
            cmake = (output / "CMakeLists.txt").read_text()
            linker = (output / "cmake/plugin_memory.ld.in").read_text()
            self.assertIn("-nostdlib", cmake)
            self.assertIn("-Wl,--emit-relocs", cmake)
            self.assertIn("plugin_manager_plugin.ld", linker)


if __name__ == "__main__":
    unittest.main()
