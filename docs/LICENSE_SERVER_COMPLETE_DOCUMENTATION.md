# 🔐 SAMPLAR 4/5 License Server - Complete Documentation

**Production Server**: `https://license-server-n5it.onrender.com`  
**Local Development**: `http://localhost:8000`

---

## 📄 **WEB PAGES (User Interface)**

### 1. **Home Page** `/`
- **URL**: https://license-server-n5it.onrender.com/
- **Method**: GET
- **Description**: Landing page with server information and navigation
- **Features**:
  - Server status
  - Quick links to all sections
  - Feature overview

### 2. **User Portal** `/portal/`
- **URL**: https://license-server-n5it.onrender.com/portal/
- **Method**: GET
- **Description**: User dashboard for managing licenses
- **Features**:
  - View active licenses
  - License details
  - Activation history

### 3. **Login Page** `/login`
- **URL**: https://license-server-n5it.onrender.com/login
- **Method**: GET
- **Description**: User login interface
- **Features**:
  - Email/password authentication
  - Session management

### 4. **Registration Page** `/register`
- **URL**: https://license-server-n5it.onrender.com/register
- **Method**: GET
- **Description**: New user registration
- **Features**:
  - Create new account
  - Email verification
  - License attachment

### 5. **User Dashboard** `/dashboard`
- **URL**: https://license-server-n5it.onrender.com/dashboard
- **Method**: GET
- **Description**: Main user dashboard after login
- **Features**:
  - License overview
  - Account settings
  - Usage statistics

---

## 🔐 **ADMIN PANEL**

### 1. **Admin Login** `/admin`
- **URL**: https://license-server-n5it.onrender.com/admin
- **Method**: GET, POST
- **Description**: Admin authentication and dashboard
- **Features**:
  - Secure admin login
  - License management
  - User management
  - System statistics

### 2. **Database Viewer** `/admin/database`
- **URL**: https://license-server-n5it.onrender.com/admin/database
- **Method**: GET
- **Description**: View database tables
- **Features**:
  - Browse all tables
  - View records
  - Search functionality

### 3. **Table Details** `/admin/database/{table_name}`
- **URL**: https://license-server-n5it.onrender.com/admin/database/licenses
- **Method**: GET
- **Description**: View specific table contents
- **Tables**: licenses, users, activations, sessions

### 4. **Database Download** `/admin/download-db`
- **URL**: https://license-server-n5it.onrender.com/admin/download-db
- **Method**: GET
- **Description**: Download database backup

### 5. **Export Licenses** `/admin/export-licenses`
- **URL**: https://license-server-n5it.onrender.com/admin/export-licenses
- **Method**: GET
- **Description**: Export licenses to CSV/JSON

### 6. **Generate License** `/admin/generate`
- **URL**: https://license-server-n5it.onrender.com/admin/generate
- **Method**: POST
- **Description**: Create new license
- **Payload**:
```json
{
  "email": "user@example.com",
  "license_type": "PRO",
  "customer_name": "John Doe",
  "max_activations": 2
}
```

### 7. **Bulk Generate** `/admin/generate-bulk`
- **URL**: https://license-server-n5it.onrender.com/admin/generate-bulk
- **Method**: POST
- **Description**: Generate multiple licenses at once

### 8. **Create User** `/admin/create-user`
- **URL**: https://license-server-n5it.onrender.com/admin/create-user
- **Method**: POST
- **Description**: Create new user account

### 9. **Attach License** `/admin/attach-license`
- **URL**: https://license-server-n5it.onrender.com/admin/attach-license
- **Method**: POST
- **Description**: Attach license to user

### 10. **Revoke License** `/admin/revoke/{license_id}`
- **URL**: https://license-server-n5it.onrender.com/admin/revoke/123
- **Method**: POST
- **Description**: Revoke/deactivate a license

### 11. **Extend Trial** `/admin/extend-trial`
- **URL**: https://license-server-n5it.onrender.com/admin/extend-trial
- **Method**: POST
- **Description**: Extend trial period for user

### 12. **Reset Trial** `/admin/reset-trial`
- **URL**: https://license-server-n5it.onrender.com/admin/reset-trial
- **Method**: POST
- **Description**: Reset trial period

### 13. **Database Migration** `/admin/database/migrate`
- **URL**: https://license-server-n5it.onrender.com/admin/database/migrate
- **Method**: POST
- **Description**: Run database migrations

### 14. **Upload Database** `/admin/upload-db`
- **URL**: https://license-server-n5it.onrender.com/admin/upload-db
- **Method**: POST
- **Description**: Upload database backup

---

## 🔌 **API ENDPOINTS (For SAMPLAR Client)**

### License Validation & Activation

#### 1. **Validate License** `/api/validate`
- **URL**: https://license-server-n5it.onrender.com/api/validate
- **Method**: POST
- **Description**: Validate existing license
- **Payload**:
```json
{
  "license_key": "SAMPLAR-XXXX-XXXX-XXXX",
  "hardware_id": "unique-hardware-fingerprint",
  "app_version": "5.0.0",
  "user_agent": "Samplar/5.0.0"
}
```
- **Response**:
```json
{
  "valid": true,
  "license_info": {
    "license_key": "SAMPLAR-XXXX-XXXX-XXXX",
    "license_type": "PRO",
    "customer_name": "John Doe",
    "email": "user@example.com"
  },
  "validation_info": {
    "validated_at": "2025-10-09T03:00:00Z",
    "hardware_id": "unique-hardware-fingerprint"
  }
}
```

#### 2. **Activate License** `/api/activate`
- **URL**: https://license-server-n5it.onrender.com/api/activate
- **Method**: POST
- **Description**: Activate license on new hardware
- **Payload**:
```json
{
  "license_key": "SAMPLAR-XXXX-XXXX-XXXX",
  "hardware_id": "unique-hardware-fingerprint",
  "email": "user@example.com",
  "password": "user-password"
}
```

#### 3. **V1 Validate** `/api/v1/validate`
- **URL**: https://license-server-n5it.onrender.com/api/v1/validate
- **Method**: POST
- **Description**: API v1 license validation endpoint

---

### Authentication Endpoints

#### 1. **Login (Web)** `/auth/login`
- **URL**: https://license-server-n5it.onrender.com/auth/login
- **Method**: GET, POST
- **Description**: Web-based login form

#### 2. **Login (JSON)** `/auth/login-json`
- **URL**: https://license-server-n5it.onrender.com/auth/login-json
- **Method**: POST
- **Description**: JSON API login
- **Payload**:
```json
{
  "email": "user@example.com",
  "password": "password"
}
```

#### 3. **Login with Hardware** `/auth/login-with-hardware`
- **URL**: https://license-server-n5it.onrender.com/auth/login-with-hardware
- **Method**: POST
- **Description**: Login and bind to hardware
- **Payload**:
```json
{
  "email": "user@example.com",
  "password": "password",
  "hardware_id": "unique-hardware-fingerprint"
}
```

#### 4. **Register** `/auth/register`
- **URL**: https://license-server-n5it.onrender.com/auth/register
- **Method**: GET, POST
- **Description**: User registration

#### 5. **Register API** `/auth/register-api`
- **URL**: https://license-server-n5it.onrender.com/auth/register-api
- **Method**: POST
- **Description**: API-based registration
- **Payload**:
```json
{
  "email": "user@example.com",
  "password": "password",
  "customer_name": "John Doe"
}
```

#### 6. **Get Current User** `/auth/me`
- **URL**: https://license-server-n5it.onrender.com/auth/me
- **Method**: GET
- **Description**: Get current authenticated user info

#### 7. **Attach License** `/auth/attach-license`
- **URL**: https://license-server-n5it.onrender.com/auth/attach-license
- **Method**: POST
- **Description**: Attach license key to user account

---

### Admin API Endpoints

#### 1. **List Licenses** `/api/v1/admin/licenses`
- **URL**: https://license-server-n5it.onrender.com/api/v1/admin/licenses
- **Method**: GET
- **Description**: Get all licenses (admin only)

#### 2. **Get License** `/api/v1/admin/licenses/{license_id}`
- **URL**: https://license-server-n5it.onrender.com/api/v1/admin/licenses/123
- **Method**: GET
- **Description**: Get specific license details

#### 3. **Delete License** `/api/v1/admin/licenses/{license_id}`
- **URL**: https://license-server-n5it.onrender.com/api/v1/admin/licenses/123
- **Method**: DELETE
- **Description**: Delete a license

#### 4. **Generate License (API)** `/api/v1/admin/generate`
- **URL**: https://license-server-n5it.onrender.com/api/v1/admin/generate
- **Method**: POST
- **Description**: Generate new license via API

---

### Client Data Form

#### 1. **Form Home** `/client-form/`
- **URL**: https://license-server-n5it.onrender.com/client-form/
- **Method**: GET
- **Description**: Client data collection form

#### 2. **Submit Form** `/client-form/submit`
- **URL**: https://license-server-n5it.onrender.com/client-form/submit
- **Method**: POST
- **Description**: Submit client data

#### 3. **View Data** `/client-form/data`
- **URL**: https://license-server-n5it.onrender.com/client-form/data
- **Method**: GET
- **Description**: View submitted client data

#### 4. **Search Clients** `/client-form/search`
- **URL**: https://license-server-n5it.onrender.com/client-form/search
- **Method**: GET
- **Description**: Search client records

#### 5. **Edit Client** `/client-form/edit/{client_id}`
- **URL**: https://license-server-n5it.onrender.com/client-form/edit/123
- **Method**: GET, POST
- **Description**: Edit client information

---

### System Endpoints

#### 1. **Health Check** `/health`
- **URL**: https://license-server-n5it.onrender.com/health
- **Method**: GET
- **Description**: Server health status
- **Response**:
```json
{
  "status": "healthy",
  "timestamp": "2025-10-09T03:00:00Z",
  "version": "1.0.0"
}
```

#### 2. **Server Info** `/info`
- **URL**: https://license-server-n5it.onrender.com/info
- **Method**: GET
- **Description**: Server information and statistics

#### 3. **API Documentation** `/docs`
- **URL**: https://license-server-n5it.onrender.com/docs
- **Method**: GET
- **Description**: Interactive API documentation (Swagger UI)

#### 4. **ReDoc** `/redoc`
- **URL**: https://license-server-n5it.onrender.com/redoc
- **Method**: GET
- **Description**: Alternative API documentation (ReDoc)

#### 5. **OpenAPI Schema** `/openapi.json`
- **URL**: https://license-server-n5it.onrender.com/openapi.json
- **Method**: GET
- **Description**: OpenAPI 3.0 schema

---

## 🧪 **TEST CREDENTIALS**

Test accounts, passwords and licence keys are not kept in this repository.
Use a real account with a SAMPL4R licence (see `src/security/README.md`).

---

## 📊 **TOTAL ENDPOINTS**

- **Total Routes**: 49
- **Web Pages**: 6
- **Admin Pages**: 14
- **API Endpoints**: 20
- **Auth Endpoints**: 10
- **System Endpoints**: 5

---

## 🔗 **QUICK LINKS**

- **Production Server**: https://license-server-n5it.onrender.com
- **API Docs**: https://license-server-n5it.onrender.com/docs
- **Admin Panel**: https://license-server-n5it.onrender.com/admin
- **User Portal**: https://license-server-n5it.onrender.com/portal/
- **Health Check**: https://license-server-n5it.onrender.com/health

---

## 📝 **NOTES**

1. All POST endpoints require proper authentication
2. Admin endpoints require admin password
3. API endpoints support both JSON and form data
4. CORS is enabled for cross-origin requests
5. Rate limiting is active (100 requests/minute)
6. SSL/TLS encryption is enforced in production
7. Database is PostgreSQL on Render.com
8. Local development uses SQLite

---

**Last Updated**: October 9, 2025  
**Server Version**: 1.0.0  
**Database**: PostgreSQL (Production) / SQLite (Local)

