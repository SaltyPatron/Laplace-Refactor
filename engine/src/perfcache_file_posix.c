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

struct laplace_perfcache_file_builder {
    laplace_perfcache_contract contract;
    char* target_path;
    char* temporary_path;
    uint8_t* mapped_address;
    size_t mapped_capacity;
    uint64_t record_count;
    uint64_t next_record_index;
    uint64_t record_stride;
    uint64_t metadata_offset;
    uint64_t maximum_metadata_bytes;
    uint64_t metadata_bytes;
    int descriptor;
    int sealed;
};

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

static char* duplicate_path(const char* path) {
    const size_t length = strlen(path);
    char* copy;
    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = (char*)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, path, length + 1u);
    }
    return copy;
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

laplace_perfcache_status laplace_perfcache_file_builder_create(
    const char* path,
    const laplace_perfcache_contract* contract,
    uint64_t record_count,
    uint64_t maximum_metadata_bytes,
    laplace_perfcache_file_builder** output_builder) {
    static const char temporary_suffix[] = ".stream.XXXXXX";
    laplace_perfcache_file_builder* builder;
    size_t capacity = 0u;
    size_t path_length;
    uint64_t record_stride;
    uint64_t records_bytes;
    void* address;
    laplace_perfcache_status status;
    if (output_builder == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *output_builder = NULL;
    if (path == NULL || path[0] == '\0' || contract == NULL ||
        record_count == 0u) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    status = laplace_perfcache_layout_measure(
        contract, record_count, maximum_metadata_bytes, &capacity);
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    if (capacity > (size_t)INT64_MAX) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    record_stride = (uint64_t)contract->key_bytes + contract->value_bytes;
    records_bytes = record_count * record_stride;
    builder = (laplace_perfcache_file_builder*)calloc(1u, sizeof(*builder));
    if (builder == NULL) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    builder->descriptor = -1;
    builder->target_path = duplicate_path(path);
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(temporary_suffix)) {
        laplace_perfcache_file_builder_destroy(&builder);
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    builder->temporary_path =
        (char*)malloc(path_length + sizeof(temporary_suffix));
    if (builder->target_path == NULL || builder->temporary_path == NULL) {
        laplace_perfcache_file_builder_destroy(&builder);
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    memcpy(builder->temporary_path, path, path_length);
    memcpy(builder->temporary_path + path_length, temporary_suffix,
           sizeof(temporary_suffix));
    builder->descriptor = mkstemp(builder->temporary_path);
    if (builder->descriptor < 0) {
        laplace_perfcache_file_builder_destroy(&builder);
        return LAPLACE_PERFCACHE_FILE_OPEN_FAILED;
    }
    (void)fcntl(builder->descriptor, F_SETFD, FD_CLOEXEC);
    if (ftruncate(builder->descriptor, (off_t)capacity) != 0) {
        laplace_perfcache_file_builder_destroy(&builder);
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    address = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED,
                   builder->descriptor, 0);
    if (address == MAP_FAILED) {
        laplace_perfcache_file_builder_destroy(&builder);
        return LAPLACE_PERFCACHE_FILE_MAPPING_FAILED;
    }
    builder->contract = *contract;
    builder->mapped_address = (uint8_t*)address;
    builder->mapped_capacity = capacity;
    builder->record_count = record_count;
    builder->record_stride = record_stride;
    builder->metadata_offset = LAPLACE_PERFCACHE_HEADER_BYTES + records_bytes;
    builder->maximum_metadata_bytes = maximum_metadata_bytes;
    *output_builder = builder;
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status laplace_perfcache_file_builder_append(
    laplace_perfcache_file_builder* builder,
    uint64_t first_record_index,
    const uint8_t* records,
    uint64_t record_count,
    const uint8_t* metadata,
    uint64_t metadata_bytes) {
    uint64_t record_bytes;
    uint64_t record_offset;
    uint64_t metadata_offset;
    if (builder == NULL || builder->sealed != 0 ||
        builder->mapped_address == NULL || builder->descriptor < 0 ||
        first_record_index != builder->next_record_index ||
        record_count > builder->record_count - builder->next_record_index ||
        (record_count != 0u && records == NULL) ||
        (metadata_bytes != 0u && metadata == NULL) ||
        metadata_bytes >
            builder->maximum_metadata_bytes - builder->metadata_bytes) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    if (record_count > UINT64_MAX / builder->record_stride) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    record_bytes = record_count * builder->record_stride;
    record_offset = LAPLACE_PERFCACHE_HEADER_BYTES +
        first_record_index * builder->record_stride;
    metadata_offset = builder->metadata_offset + builder->metadata_bytes;
    if (record_offset > SIZE_MAX || record_bytes > SIZE_MAX ||
        metadata_offset > SIZE_MAX || metadata_bytes > SIZE_MAX) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    if (record_bytes != 0u) {
        memcpy(builder->mapped_address + (size_t)record_offset,
               records, (size_t)record_bytes);
    }
    if (metadata_bytes != 0u) {
        memcpy(builder->mapped_address + (size_t)metadata_offset,
               metadata, (size_t)metadata_bytes);
    }
    builder->next_record_index += record_count;
    builder->metadata_bytes += metadata_bytes;
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status laplace_perfcache_file_builder_seal(
    laplace_perfcache_file_builder* builder,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    laplace_perfcache_view_validator view_validator,
    void* view_validator_context,
    uint64_t* invalid_record_index,
    size_t* artifact_bytes,
    laplace_digest256* artifact_digest) {
    laplace_perfcache_view view;
    laplace_perfcache_mapping written;
    laplace_perfcache_status status;
    uint64_t written_invalid = UINT64_MAX;
    int sync_result;
    if (builder == NULL || validator == NULL || invalid_record_index == NULL ||
        artifact_bytes == NULL || artifact_digest == NULL ||
        builder->sealed != 0 || builder->mapped_address == NULL ||
        builder->descriptor < 0 ||
        builder->next_record_index != builder->record_count) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *invalid_record_index = UINT64_MAX;
    status = laplace_perfcache_layout_seal(
        &builder->contract, builder->record_count, builder->metadata_bytes,
        builder->mapped_address, builder->mapped_capacity,
        artifact_bytes, artifact_digest);
    if (status == LAPLACE_PERFCACHE_OK) {
        status = laplace_perfcache_validate(
            builder->mapped_address, *artifact_bytes,
            &builder->contract, &view);
    }
    if (status == LAPLACE_PERFCACHE_OK) {
        status = laplace_perfcache_validate_records(
            &view, validator, validator_context, invalid_record_index);
    }
    if (status == LAPLACE_PERFCACHE_OK && view_validator != NULL) {
        status = view_validator(
            view_validator_context, &view, invalid_record_index);
    }
    if (status == LAPLACE_PERFCACHE_OK &&
        msync(builder->mapped_address, *artifact_bytes, MS_SYNC) != 0) {
        status = LAPLACE_PERFCACHE_FILE_SYNC_FAILED;
    }
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    if (munmap(builder->mapped_address, builder->mapped_capacity) != 0) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    builder->mapped_address = NULL;
    builder->mapped_capacity = 0u;
    if (ftruncate(builder->descriptor, (off_t)*artifact_bytes) != 0 ||
        fchmod(builder->descriptor, S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    do {
        sync_result = fsync(builder->descriptor);
    } while (sync_result != 0 && errno == EINTR);
    if (sync_result != 0) {
        return LAPLACE_PERFCACHE_FILE_SYNC_FAILED;
    }
    if (close_descriptor(builder->descriptor) != LAPLACE_PERFCACHE_OK) {
        builder->descriptor = -1;
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    builder->descriptor = -1;
    memset(&written, 0, sizeof(written));
    written.native_handle = -1;
    status = laplace_perfcache_mapping_open(
        builder->temporary_path, &builder->contract, validator,
        validator_context, &written_invalid, &written);
    if (status == LAPLACE_PERFCACHE_OK && view_validator != NULL) {
        status = view_validator(
            view_validator_context, &written.view, &written_invalid);
    }
    if (status == LAPLACE_PERFCACHE_OK &&
        memcmp(written.view.artifact_digest.bytes, artifact_digest->bytes,
               sizeof(artifact_digest->bytes)) != 0) {
        status = LAPLACE_PERFCACHE_DIGEST_MISMATCH;
    }
    if (written.native_handle >= 0 &&
        laplace_perfcache_mapping_close(&written) != LAPLACE_PERFCACHE_OK &&
        status == LAPLACE_PERFCACHE_OK) {
        status = LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    if (written_invalid != UINT64_MAX) {
        *invalid_record_index = written_invalid;
    }
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    if (link(builder->temporary_path, builder->target_path) != 0) {
        if (errno == EEXIST) {
            laplace_perfcache_mapping existing;
            uint64_t existing_invalid = UINT64_MAX;
            memset(&existing, 0, sizeof(existing));
            existing.native_handle = -1;
            status = laplace_perfcache_mapping_open(
                builder->target_path, &builder->contract, validator,
                validator_context, &existing_invalid, &existing);
            if (status == LAPLACE_PERFCACHE_OK && view_validator != NULL) {
                status = view_validator(
                    view_validator_context, &existing.view,
                    &existing_invalid);
            }
            if (status == LAPLACE_PERFCACHE_OK) {
                status = memcmp(
                    existing.view.artifact_digest.bytes,
                    artifact_digest->bytes,
                    sizeof(artifact_digest->bytes)) == 0
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
        return status;
    }
    if (unlink(builder->temporary_path) != 0) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    }
    builder->temporary_path[0] = '\0';
    status = sync_parent_directory(builder->target_path);
    if (status == LAPLACE_PERFCACHE_OK) {
        builder->sealed = 1;
    }
    return status;
}

void laplace_perfcache_file_builder_destroy(
    laplace_perfcache_file_builder** builder) {
    laplace_perfcache_file_builder* value;
    if (builder == NULL || *builder == NULL) {
        return;
    }
    value = *builder;
    if (value->mapped_address != NULL && value->mapped_capacity != 0u) {
        (void)munmap(value->mapped_address, value->mapped_capacity);
    }
    if (value->descriptor >= 0) {
        (void)close_descriptor(value->descriptor);
    }
    if (value->temporary_path != NULL && value->temporary_path[0] != '\0') {
        (void)unlink(value->temporary_path);
    }
    free(value->temporary_path);
    free(value->target_path);
    free(value);
    *builder = NULL;
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
