#define _POSIX_C_SOURCE 200809L

#include "laplace/perfcache.h"
#include "laplace/perfcache_registry.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "blake3.h"

static laplace_perfcache_status close_descriptor(int descriptor) {
    int result;
    do {
        result = close(descriptor);
    } while (result != 0 && errno == EINTR);
    return result == 0
        ? LAPLACE_PERFCACHE_OK
        : LAPLACE_PERFCACHE_FILE_IO_FAILED;
}

static laplace_perfcache_status write_complete(
    int descriptor,
    const uint8_t* bytes,
    size_t byte_count) {
    size_t written = 0;
    while (written < byte_count) {
        ssize_t result = write(descriptor, bytes + written, byte_count - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return LAPLACE_PERFCACHE_FILE_IO_FAILED;
        }
        written += (size_t)result;
    }
    return LAPLACE_PERFCACHE_OK;
}

static char* directory_path(const char* path) {
    const char* separator = strrchr(path, '/');
    char* directory;
    size_t length;
    if (separator == NULL) {
        directory = (char*)malloc(2u);
        if (directory != NULL) {
            directory[0] = '.';
            directory[1] = '\0';
        }
        return directory;
    }
    length = separator == path ? 1u : (size_t)(separator - path);
    directory = (char*)malloc(length + 1u);
    if (directory != NULL) {
        memcpy(directory, path, length);
        directory[length] = '\0';
    }
    return directory;
}

static laplace_perfcache_status sync_parent_directory(const char* path) {
    char* directory = directory_path(path);
    struct stat directory_status;
    int descriptor;
    int sync_result;
    laplace_perfcache_status close_status;
    if (directory == NULL) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    descriptor = open(directory, O_RDONLY | O_CLOEXEC);
    free(directory);
    if (descriptor < 0) {
        return LAPLACE_PERFCACHE_FILE_OPEN_FAILED;
    }
    if (fstat(descriptor, &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode)) {
        (void)close_descriptor(descriptor);
        return LAPLACE_PERFCACHE_FILE_TYPE_INVALID;
    }
    do {
        sync_result = fsync(descriptor);
    } while (sync_result != 0 && errno == EINTR);
    close_status = close_descriptor(descriptor);
    if (sync_result != 0) {
        return LAPLACE_PERFCACHE_FILE_SYNC_FAILED;
    }
    return close_status;
}

laplace_perfcache_status laplace_perfcache_mapping_open(
    const char* path,
    const laplace_perfcache_contract* expected_contract,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    uint64_t* invalid_record_index,
    laplace_perfcache_mapping* mapping) {
    struct stat file_status;
    laplace_perfcache_mapping result;
    laplace_perfcache_status status;
    int descriptor;
    void* address;

    if (path == NULL || path[0] == '\0' || expected_contract == NULL ||
        validator == NULL || invalid_record_index == NULL || mapping == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *invalid_record_index = UINT64_MAX;
    memset(&result, 0, sizeof(result));
    result.native_handle = -1;
    descriptor = open(path, O_RDONLY | O_CLOEXEC
#if defined(O_NOFOLLOW)
        | O_NOFOLLOW
#endif
    );
    if (descriptor < 0) {
        return LAPLACE_PERFCACHE_FILE_OPEN_FAILED;
    }
    if (fstat(descriptor, &file_status) != 0) {
        (void)close_descriptor(descriptor);
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    if (!S_ISREG(file_status.st_mode) || file_status.st_size <= 0 ||
        (uintmax_t)file_status.st_size > SIZE_MAX) {
        (void)close_descriptor(descriptor);
        return LAPLACE_PERFCACHE_FILE_TYPE_INVALID;
    }
    address = mmap(NULL,
                   (size_t)file_status.st_size,
                   PROT_READ,
                   MAP_PRIVATE,
                   descriptor,
                   0);
    if (address == MAP_FAILED) {
        (void)close_descriptor(descriptor);
        return LAPLACE_PERFCACHE_FILE_MAPPING_FAILED;
    }
    status = laplace_perfcache_validate(
        (const uint8_t*)address,
        (size_t)file_status.st_size,
        expected_contract,
        &result.view);
    if (status != LAPLACE_PERFCACHE_OK) {
        (void)munmap(address, (size_t)file_status.st_size);
        (void)close_descriptor(descriptor);
        return status;
    }
    status = laplace_perfcache_validate_records(
        &result.view, validator, validator_context, invalid_record_index);
    if (status != LAPLACE_PERFCACHE_OK) {
        (void)munmap(address, (size_t)file_status.st_size);
        (void)close_descriptor(descriptor);
        return status;
    }
    result.mapped_address = (const uint8_t*)address;
    result.mapped_bytes = (size_t)file_status.st_size;
    result.native_handle = descriptor;
    result.device_id = (uint64_t)file_status.st_dev;
    result.file_id = (uint64_t)file_status.st_ino;
    *mapping = result;
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status laplace_perfcache_mapping_close(
    laplace_perfcache_mapping* mapping) {
    laplace_perfcache_status result = LAPLACE_PERFCACHE_OK;
    if (mapping == NULL || mapping->mapped_address == NULL ||
        mapping->mapped_bytes == 0u || mapping->native_handle < 0) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    if (munmap((void*)mapping->mapped_address, mapping->mapped_bytes) != 0) {
        result = LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    if (close_descriptor((int)mapping->native_handle) != LAPLACE_PERFCACHE_OK) {
        result = LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    memset(mapping, 0, sizeof(*mapping));
    mapping->native_handle = -1;
    return result;
}

laplace_perfcache_status laplace_perfcache_publish_file(
    const char* path,
    const uint8_t* artifact,
    size_t artifact_bytes,
    const laplace_perfcache_contract* expected_contract,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    uint64_t* invalid_record_index) {
    static const char temporary_suffix[] = ".write.XXXXXX";
    laplace_perfcache_view view;
    laplace_perfcache_status status;
    char* temporary_path;
    size_t path_length;
    int descriptor;
    int sync_result;

    if (path == NULL || path[0] == '\0' || artifact == NULL ||
        expected_contract == NULL || validator == NULL ||
        invalid_record_index == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *invalid_record_index = UINT64_MAX;
    status = laplace_perfcache_validate(
        artifact, artifact_bytes, expected_contract, &view);
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    status = laplace_perfcache_validate_records(
        &view, validator, validator_context, invalid_record_index);
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(temporary_suffix)) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    temporary_path = (char*)malloc(path_length + sizeof(temporary_suffix));
    if (temporary_path == NULL) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    memcpy(temporary_path, path, path_length);
    memcpy(temporary_path + path_length,
           temporary_suffix,
           sizeof(temporary_suffix));
    descriptor = mkstemp(temporary_path);
    if (descriptor < 0) {
        free(temporary_path);
        return LAPLACE_PERFCACHE_FILE_OPEN_FAILED;
    }
    (void)fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    if (fchmod(descriptor, S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        status = LAPLACE_PERFCACHE_FILE_IO_FAILED;
    } else {
        status = write_complete(descriptor, artifact, artifact_bytes);
    }
    if (status == LAPLACE_PERFCACHE_OK) {
#if defined(LAPLACE_TEST_CORRUPT_PERFCACHE_TEMPORARY_WRITE)
        uint8_t corrupted = (uint8_t)(artifact[LAPLACE_PERFCACHE_HEADER_BYTES] ^ 1u);
        if (pwrite(descriptor, &corrupted, 1u,
                   (off_t)LAPLACE_PERFCACHE_HEADER_BYTES) != 1) {
            status = LAPLACE_PERFCACHE_FILE_IO_FAILED;
        }
#endif
    }
    if (status == LAPLACE_PERFCACHE_OK) {
        do {
            sync_result = fsync(descriptor);
        } while (sync_result != 0 && errno == EINTR);
        if (sync_result != 0) {
            status = LAPLACE_PERFCACHE_FILE_SYNC_FAILED;
        }
    }
    if (close_descriptor(descriptor) != LAPLACE_PERFCACHE_OK &&
        status == LAPLACE_PERFCACHE_OK) {
        status = LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    if (status == LAPLACE_PERFCACHE_OK) {
        laplace_perfcache_mapping written;
        uint64_t written_invalid = UINT64_MAX;
        memset(&written, 0, sizeof(written));
        written.native_handle = -1;
        status = laplace_perfcache_mapping_open(
            temporary_path, expected_contract, validator, validator_context,
            &written_invalid, &written);
        if (status == LAPLACE_PERFCACHE_OK) {
            if (memcmp(written.view.artifact_digest.bytes,
                       view.artifact_digest.bytes,
                       sizeof(view.artifact_digest.bytes)) != 0) {
                status = LAPLACE_PERFCACHE_DIGEST_MISMATCH;
            }
            if (laplace_perfcache_mapping_close(&written) !=
                    LAPLACE_PERFCACHE_OK &&
                status == LAPLACE_PERFCACHE_OK) {
                status = LAPLACE_PERFCACHE_FILE_IO_FAILED;
            }
        }
        if (written_invalid != UINT64_MAX) {
            *invalid_record_index = written_invalid;
        }
    }
    if (status == LAPLACE_PERFCACHE_OK) {
        if (link(temporary_path, path) == 0) {
        } else if (errno == EEXIST) {
            laplace_perfcache_mapping existing;
            uint64_t existing_invalid = UINT64_MAX;
            memset(&existing, 0, sizeof(existing));
            existing.native_handle = -1;
            status = laplace_perfcache_mapping_open(
                path, expected_contract, validator, validator_context,
                &existing_invalid, &existing);
            if (status == LAPLACE_PERFCACHE_OK) {
                status = memcmp(existing.view.artifact_digest.bytes,
                                view.artifact_digest.bytes,
                                sizeof(view.artifact_digest.bytes)) == 0
                    ? LAPLACE_PERFCACHE_OK
                    : LAPLACE_PERFCACHE_ARTIFACT_CONFLICT;
                (void)laplace_perfcache_mapping_close(&existing);
            } else {
                status = LAPLACE_PERFCACHE_ARTIFACT_CONFLICT;
            }
        } else {
            status = LAPLACE_PERFCACHE_FILE_RENAME_FAILED;
        }
    }
    if (status != LAPLACE_PERFCACHE_OK) {
        (void)unlink(temporary_path);
        free(temporary_path);
        return status;
    }
    (void)unlink(temporary_path);
    free(temporary_path);
    return sync_parent_directory(path);
}

static laplace_perfcache_status file_provider_open(
    void* state,
    const char* path,
    const laplace_perfcache_contract* expected_contract,
    const laplace_digest256* expected_artifact_digest,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    uint64_t* invalid_record_index,
    laplace_perfcache_artifact_handle* handle) {
    laplace_perfcache_mapping* mapping;
    laplace_perfcache_status status;
    (void)state;
    static const uint8_t domain[] =
        "laplace-posix-perfcache-loaded-object-v1";
    blake3_hasher hasher;
    uint8_t scalar[8];
    size_t index;
    if (handle == NULL || expected_artifact_digest == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    memset(handle, 0, sizeof(*handle));
    mapping = (laplace_perfcache_mapping*)malloc(sizeof(*mapping));
    if (mapping == NULL) {
        return LAPLACE_PERFCACHE_FILE_MAPPING_FAILED;
    }
    memset(mapping, 0, sizeof(*mapping));
    mapping->native_handle = -1;
    status = laplace_perfcache_mapping_open(
        path, expected_contract, validator, validator_context,
        invalid_record_index, mapping);
    if (status != LAPLACE_PERFCACHE_OK) {
        free(mapping);
        return status;
    }
    if (memcmp(mapping->view.artifact_digest.bytes,
               expected_artifact_digest->bytes,
               sizeof(expected_artifact_digest->bytes)) != 0) {
        (void)laplace_perfcache_mapping_close(mapping);
        free(mapping);
        return LAPLACE_PERFCACHE_DIGEST_MISMATCH;
    }
    handle->view = mapping->view;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(&hasher, mapping->view.artifact_digest.bytes,
                         sizeof(mapping->view.artifact_digest.bytes));
    for (index = 0; index < sizeof(scalar); ++index) {
        scalar[index] = (uint8_t)(mapping->device_id >> (index * 8u));
    }
    blake3_hasher_update(&hasher, scalar, sizeof(scalar));
    for (index = 0; index < sizeof(scalar); ++index) {
        scalar[index] = (uint8_t)(mapping->file_id >> (index * 8u));
    }
    blake3_hasher_update(&hasher, scalar, sizeof(scalar));
    for (index = 0; index < sizeof(scalar); ++index) {
        scalar[index] = (uint8_t)((uint64_t)mapping->mapped_bytes >> (index * 8u));
    }
    blake3_hasher_update(&hasher, scalar, sizeof(scalar));
    blake3_hasher_finalize(&hasher, handle->loaded_identity.bytes,
                           sizeof(handle->loaded_identity.bytes));
    handle->provider_handle = mapping;
    return LAPLACE_PERFCACHE_OK;
}

static laplace_perfcache_status file_provider_prefault(
    void* state,
    laplace_perfcache_artifact_handle* handle,
    const laplace_execution_grant* resource_grant,
    uint64_t* touched_bytes,
    uint64_t* touched_pages) {
    laplace_perfcache_mapping* mapping;
    long page_size_result;
    size_t page_size;
    size_t offset;
    uint8_t observed = 0;
    (void)state;
    if (handle == NULL || handle->provider_handle == NULL ||
        resource_grant == NULL ||
        touched_bytes == NULL || touched_pages == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    mapping = (laplace_perfcache_mapping*)handle->provider_handle;
    if (mapping->mapped_address == NULL || mapping->mapped_bytes == 0u ||
        mapping->mapped_bytes > resource_grant->memory_bytes) {
        return LAPLACE_PERFCACHE_FILE_MAPPING_FAILED;
    }
    page_size_result = sysconf(_SC_PAGESIZE);
    if (page_size_result <= 0) {
        return LAPLACE_PERFCACHE_FILE_MAPPING_FAILED;
    }
    page_size = (size_t)page_size_result;
    for (offset = 0; offset < mapping->mapped_bytes;) {
        const volatile uint8_t* address = mapping->mapped_address + offset;
        observed = (uint8_t)(observed ^ *address);
        if (mapping->mapped_bytes - offset <= page_size) {
            break;
        }
        offset += page_size;
    }
    if ((mapping->mapped_bytes - 1u) % page_size != 0u) {
        const volatile uint8_t* address =
            mapping->mapped_address + mapping->mapped_bytes - 1u;
        observed = (uint8_t)(observed ^ *address);
    }
    *touched_bytes = mapping->mapped_bytes;
    *touched_pages =
        (uint64_t)(1u + (mapping->mapped_bytes - 1u) / page_size);
    (void)observed;
    return LAPLACE_PERFCACHE_OK;
}

static void file_provider_close(
    void* state,
    laplace_perfcache_artifact_handle* handle) {
    laplace_perfcache_mapping* mapping;
    (void)state;
    if (handle == NULL || handle->provider_handle == NULL) {
        return;
    }
    mapping = (laplace_perfcache_mapping*)handle->provider_handle;
    (void)laplace_perfcache_mapping_close(mapping);
    free(mapping);
    memset(handle, 0, sizeof(*handle));
}

laplace_perfcache_registry_status laplace_perfcache_file_provider(
    laplace_perfcache_artifact_provider_v1* provider) {
    static const uint8_t domain[] = "laplace-posix-perfcache-file-provider-v1";
    blake3_hasher hasher;
    if (provider == NULL) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    memset(provider, 0, sizeof(*provider));
    provider->open = file_provider_open;
    provider->prefault = file_provider_prefault;
    provider->close = file_provider_close;
    provider->abi_major = LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MINOR;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_finalize(
        &hasher, provider->provider_fingerprint.bytes,
        sizeof(provider->provider_fingerprint.bytes));
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}
