export interface paths {
    "/api/v1/av/admin/organizations/{orgId}/operations": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator or current organization member with av.manage; does not require participation in the resource. API keys and resource credentials are rejected. */
        get: operations["av_admin_get_organizations_orgId_operations"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/organizations/{orgId}/operations/{id}": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator or current organization member with av.manage; does not require participation in the resource. API keys and resource credentials are rejected. */
        get: operations["av_admin_get_organizations_orgId_operations_id"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/organizations/{orgId}/overview": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator or current organization member with av.manage; does not require participation in the resource. API keys and resource credentials are rejected. */
        get: operations["av_admin_get_organizations_orgId_overview"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/organizations/{orgId}/resources": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator or current organization member with av.manage; does not require participation in the resource. API keys and resource credentials are rejected. */
        get: operations["av_admin_get_organizations_orgId_resources"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/organizations/{orgId}/resources/{id}": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator or current organization member with av.manage; does not require participation in the resource. API keys and resource credentials are rejected. */
        get: operations["av_admin_get_organizations_orgId_resources_id"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/organizations/{orgId}/resources/{id}/actions": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /** @description Platform administrator or current organization member with av.manage; does not require participation in the resource. API keys and resource credentials are rejected. */
        post: operations["av_admin_post_organizations_orgId_resources_id_actions"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/platform/media-nodes": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator session required; API keys and resource credentials are rejected. */
        get: operations["av_admin_get_platform_media_nodes"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/platform/operations": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator session required; API keys and resource credentials are rejected. */
        get: operations["av_admin_get_platform_operations"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/platform/overview": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator session required; API keys and resource credentials are rejected. */
        get: operations["av_admin_get_platform_overview"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/api/v1/av/admin/platform/resources": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description Platform administrator session required; API keys and resource credentials are rejected. */
        get: operations["av_admin_get_platform_resources"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
}
export type webhooks = Record<string, never>;
export interface components {
    schemas: {
        AVAdminAction: {
            /** @enum {string} */
            action: "end" | "cancel" | "revoke-control" | "kick" | "mute" | "request-unmute" | "chat-mute" | "chat-unmute" | "stage-remove";
            reason: string;
            target?: string;
            /**
             * Format: int64
             * @description Optional expected resource version; stale writes return 409
             */
            version?: number;
        };
        AVAdminActionResult: {
            accepted: boolean;
            action_id: string;
            id: string;
            operation_ids: string[];
            state: string;
            /** Format: int64 */
            version: number;
        };
        AVAdminDetail: {
            actions: {
                action: string;
                actor: string;
                /** Format: int64 */
                created: number;
                id: string;
                operation_ids: string[];
                reason: string;
                target: string;
            }[];
            bindings: {
                /** Format: int64 */
                created: number;
                /** Format: int64 */
                generation: number;
                id: string;
                node_id: string;
                purpose: string;
                state: string;
            }[];
            details: {
                [key: string]: unknown;
            };
            members: {
                banned: boolean;
                muted: boolean;
                name: string;
                /** Format: int64 */
                policy_version: number;
                role: string;
                stage: string;
                state: string;
                subject_id: string;
                /** Format: int64 */
                unmute_request: number;
                /** Format: int64 */
                updated: number;
            }[];
            members_truncated: boolean;
            resource: {
                business: string;
                /** Format: int64 */
                created: number;
                id: string;
                kind: string;
                module_enabled: boolean;
                organization_id: string;
                owner: string;
                state: string;
                title: string;
                /** Format: int64 */
                updated: number;
                /** Format: int64 */
                version: number;
            };
        };
        AVAdminNode: {
            /** Format: int64 */
            bindings: number;
            /** Format: int64 */
            capacity: number;
            configured: boolean;
            enabled: boolean;
            healthy: boolean;
            id: string;
            incarnation: string;
            status: string;
            /** Format: int64 */
            updated: number;
        };
        AVAdminOperation: {
            action: string;
            binding_id: string;
            /** Format: int64 */
            created: number;
            id: string;
            job_id: string;
            organization_id: string;
            resource_id: string;
            state: string;
            subject_id: string;
            /** Format: int64 */
            updated: number;
        };
        AVAdminOperationPage: {
            items: {
                action: string;
                binding_id: string;
                /** Format: int64 */
                created: number;
                id: string;
                job_id: string;
                organization_id: string;
                resource_id: string;
                state: string;
                subject_id: string;
                /** Format: int64 */
                updated: number;
            }[];
            /** Format: int64 */
            limit: number;
            /** Format: int64 */
            offset: number;
            /** Format: int64 */
            total: number;
        };
        AVAdminOverview: {
            /** Format: int64 */
            created_last_24h: number;
            /** Format: int64 */
            generated_at: number;
            operations: {
                business?: string;
                /** Format: int64 */
                count: number;
                state: string;
            }[];
            resources: {
                business?: string;
                /** Format: int64 */
                count: number;
                state: string;
            }[];
        };
        AVAdminResource: {
            business: string;
            /** Format: int64 */
            created: number;
            id: string;
            kind: string;
            module_enabled: boolean;
            organization_id: string;
            owner: string;
            state: string;
            title: string;
            /** Format: int64 */
            updated: number;
            /** Format: int64 */
            version: number;
        };
        AVAdminResourcePage: {
            items: {
                business: string;
                /** Format: int64 */
                created: number;
                id: string;
                kind: string;
                module_enabled: boolean;
                organization_id: string;
                owner: string;
                state: string;
                title: string;
                /** Format: int64 */
                updated: number;
                /** Format: int64 */
                version: number;
            }[];
            /** Format: int64 */
            limit: number;
            /** Format: int64 */
            offset: number;
            /** Format: int64 */
            total: number;
        };
        AVMediaOperation: {
            action: string;
            id: string;
            resource_id: string;
            /** @enum {string} */
            state: "queued" | "retrying" | "succeeded" | "superseded" | "failed";
            /**
             * Format: int64
             * @description Unix milliseconds
             */
            updated: number;
        };
        Actor: {
            organization: string;
            permissions: string[];
            role: string;
            scope: string;
            user: components["schemas"]["User"];
        };
        Auth: {
            user: components["schemas"]["User"];
        };
        Capabilities: {
            brand?: string;
            modules: string[];
            organization_mode: string;
            registration: boolean;
            version: string;
        };
        DisableInput: {
            disabled: boolean;
        };
        Empty: Record<string, never>;
        Error: {
            error: {
                code: string;
                message: string;
                retryable: boolean;
            };
            request_id: string;
        };
        GrantInput: {
            expires?: number;
            item: string;
            key: string;
            owner: string;
            reason: string;
            units: number;
        };
        ID: {
            id: string;
        };
        InviteInput: {
            role: string;
            username: string;
        };
        IssuedKey: {
            key: components["schemas"]["Key"];
            raw: string;
        };
        Job: {
            attempt: number;
            available: number;
            created: number;
            error: string;
            fence: number;
            id: string;
            kind: string;
            lease: number;
            max_attempts: number;
            owner: string;
            payload: unknown;
            progress: number;
            result: unknown;
            scope: string;
            /** @enum {string} */
            status: "pending" | "running" | "done" | "failed" | "cancelled";
            type: string;
            updated: number;
        };
        JobInput: {
            available?: number;
            key: string;
            max_attempts?: number;
            payload: unknown;
            type: string;
        };
        Key: {
            expires: string;
            id: string;
            prefix: string;
            revoked: boolean;
            scopes: string[];
            user_id: string;
        };
        KeyInput: {
            scopes: ("read" | "write")[];
            ttl_hours: number;
        };
        LoginInput: {
            name: string;
            password: string;
        };
        MailInput: {
            body?: string;
            data?: {
                [key: string]: string;
            };
            key: string;
            subject?: string;
            template?: string;
            to: string;
        };
        NameInput: {
            name: string;
        };
        NotificationInput: {
            body: string;
            key: string;
            title: string;
            user: string;
        };
        OK: {
            ok: boolean;
        };
        Operations: {
            [key: string]: unknown;
        };
        OrganizationDirectoryPage: {
            items: components["schemas"]["Record"][];
            limit: number;
            offset: number;
            total: number;
        };
        PageJob: {
            items: components["schemas"]["Job"][];
            limit: number;
            offset: number;
        };
        PageKey: {
            items: components["schemas"]["Key"][];
            limit: number;
            offset: number;
        };
        PageRecord: {
            items: components["schemas"]["Record"][];
            limit: number;
            offset: number;
        };
        PageSession: {
            items: components["schemas"]["Session"][];
            limit: number;
            offset: number;
        };
        PageUser: {
            items: components["schemas"]["User"][];
            limit: number;
            offset: number;
        };
        PasswordInput: {
            old_password: string;
            password: string;
        };
        Permissions: {
            catalog: string[];
            effective: string[];
        };
        Record: {
            /** Format: date-time */
            created: string;
            data: unknown;
            id: string;
            kind: string;
            owner: string;
            scope: string;
            /** Format: date-time */
            updated: string;
            version: number;
        };
        Records: components["schemas"]["Record"][];
        RegisterInput: {
            name: string;
            password: string;
        };
        ReservationInput: {
            item: string;
            key: string;
            units: number;
        };
        ResetInput: {
            password: string;
            token: string;
        };
        RevokeInput: {
            user?: string;
        };
        Role: {
            name: string;
            permissions: string[];
        };
        RoleInput: {
            role?: string;
        };
        Session: {
            /** Format: date-time */
            expires: string;
            id: string;
            user_id: string;
        };
        SettingInput: {
            value: unknown;
        };
        Strings: string[];
        Template: {
            body: string;
            subject: string;
        };
        Token: {
            token: string;
        };
        Totals: {
            [key: string]: number;
        };
        URL: {
            url: string;
        };
        User: {
            admin: boolean;
            disabled: boolean;
            id: string;
            name: string;
            tenant_id: string;
        };
    };
    responses: never;
    parameters: never;
    requestBodies: never;
    headers: never;
    pathItems: never;
}
export type $defs = Record<string, never>;
export interface operations {
    av_admin_get_organizations_orgId_operations: {
        parameters: {
            query?: {
                business?: string;
                state?: string;
                owner?: string;
                q?: string;
                from?: string;
                to?: string;
                resource_id?: string;
                limit?: number;
                offset?: number;
            };
            header?: never;
            path: {
                orgId: string;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminOperationPage"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_organizations_orgId_operations_id: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                orgId: string;
                id: string;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminOperation"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_organizations_orgId_overview: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                orgId: string;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminOverview"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_organizations_orgId_resources: {
        parameters: {
            query?: {
                business?: string;
                state?: string;
                owner?: string;
                q?: string;
                from?: string;
                to?: string;
                resource_id?: string;
                limit?: number;
                offset?: number;
            };
            header?: never;
            path: {
                orgId: string;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminResourcePage"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_organizations_orgId_resources_id: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                orgId: string;
                id: string;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminDetail"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_post_organizations_orgId_resources_id_actions: {
        parameters: {
            query?: never;
            header: {
                "X-Foundation-Request": "1";
                "Idempotency-Key": string;
            };
            path: {
                orgId: string;
                id: string;
            };
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["AVAdminAction"];
            };
        };
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminActionResult"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_platform_media_nodes: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": {
                        items: components["schemas"]["AVAdminNode"][];
                    };
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_platform_operations: {
        parameters: {
            query?: {
                business?: string;
                state?: string;
                owner?: string;
                q?: string;
                from?: string;
                to?: string;
                resource_id?: string;
                limit?: number;
                offset?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminOperationPage"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_platform_overview: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminOverview"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    av_admin_get_platform_resources: {
        parameters: {
            query?: {
                business?: string;
                state?: string;
                owner?: string;
                q?: string;
                from?: string;
                to?: string;
                resource_id?: string;
                limit?: number;
                offset?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Management result. Accepted media changes finish asynchronously. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AVAdminResourcePage"];
                };
            };
            /** @description Invalid filter/action or missing idempotency key */
            400: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Login session required */
            401: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Management permission denied */
            403: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource not found in the selected organization */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
            /** @description Resource state, version or idempotency conflict */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
}
