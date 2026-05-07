# SAMPLAR Community Hub - Complete Technical Specification

**Version:** 1.0
**Date:** December 15, 2025
**Author:** Development Team
**Status:** Design Document - Ready for Implementation

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [System Architecture](#2-system-architecture)
3. [Server-Side Implementation](#3-server-side-implementation)
4. [Client-Side Implementation (SAMPLAR)](#4-client-side-implementation-SAMPLAR)
5. [Database Schema](#5-database-schema)
6. [API Specification](#6-api-specification)
7. [Authentication Flow](#7-authentication-flow)
8. [UI/UX Design](#8-uiux-design)
9. [Stripe Integration](#9-stripe-integration)
10. [Deployment Guide](#10-deployment-guide)
11. [Testing Strategy](#11-testing-strategy)
12. [Security Considerations](#12-security-considerations)

---

## 1. Executive Summary

### 1.1 Purpose
Add an integrated community hub to SAMPLAR that allows users to:
- Participate in forum discussions
- Ask and answer questions (Q&A/FAQ)
- Donate to support development
- Discover other products (Varia, etc.)
- Chat with developers and other users

### 1.2 Architecture Overview
```
┌─────────────────────┐         ┌─────────────────────────────────────┐
│      SAMPLAR App        │         │     Render.com License Server       │
│  (JUCE/C++ Client)  │         │        (FastAPI/Python)             │
├─────────────────────┤         ├─────────────────────────────────────┤
│                     │         │                                     │
│  WebBrowserComponent│◄───────►│  community_hub plugin               │
│  displays website   │  HTTPS  │  ├── Forum                          │
│                     │         │  ├── Q&A/FAQ                        │
│  Auth token passed  │         │  ├── Donations (Stripe)             │
│  via URL parameter  │         │  └── Product Showcase               │
│                     │         │                                     │
└─────────────────────┘         └─────────────────────────────────────┘
```

### 1.3 Key Benefits
- **Zero new accounts** - Uses existing license system users
- **Zero new auth** - Uses existing session tokens
- **Minimal SAMPLAR changes** - Just add WebBrowserComponent page
- **Plugin architecture** - Server-side follows BIBLE.MD

---

## 2. System Architecture

### 2.1 High-Level Architecture
```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              COMPLETE SYSTEM                                  │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                         SAMPLAR DESKTOP APPLICATION                        │ │
│  ├────────────────────────────────────────────────────────────────────────┤ │
│  │                                                                        │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────────┐  │ │
│  │  │   Samples   │ │ Wave Editor │ │ Variations  │ │   COMMUNITY     │  │ │
│  │  │    Page     │ │    Page     │ │    Page     │ │     PAGE        │  │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └────────┬────────┘  │ │
│  │                                                           │            │ │
│  │                                              ┌────────────▼──────────┐ │ │
│  │                                              │ WebBrowserComponent   │ │ │
│  │                                              │ (JUCE built-in)       │ │ │
│  │                                              └────────────┬──────────┘ │ │
│  │                                                           │            │ │
│  └───────────────────────────────────────────────────────────┼────────────┘ │
│                                                              │              │
│                                    HTTPS + Auth Token        │              │
│                                                              ▼              │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                      RENDER.COM LICENSE SERVER                         │ │
│  ├────────────────────────────────────────────────────────────────────────┤ │
│  │                                                                        │ │
│  │  ┌─────────────────────────────────────────────────────────────────┐  │ │
│  │  │                         FastAPI Application                      │  │ │
│  │  ├─────────────────────────────────────────────────────────────────┤  │ │
│  │  │                                                                 │  │ │
│  │  │  EXISTING PLUGINS:              NEW PLUGIN:                     │  │ │
│  │  │  ┌─────────────────┐           ┌─────────────────────────────┐ │  │ │
│  │  │  │ license_api     │           │     community_hub           │ │  │ │
│  │  │  │ admin_interface │           │  ├── Forum Service          │ │  │ │
│  │  │  │ database_manager│◄─────────►│  ├── FAQ Service            │ │  │ │
│  │  │  │ crypto_service  │           │  ├── Donation Service       │ │  │ │
│  │  │  │ security_manager│           │  └── Product Service        │ │  │ │
│  │  │  └─────────────────┘           └─────────────────────────────┘ │  │ │
│  │  │                                                                 │  │ │
│  │  └─────────────────────────────────────────────────────────────────┘  │ │
│  │                                    │                                   │ │
│  │                                    ▼                                   │ │
│  │  ┌─────────────────────────────────────────────────────────────────┐  │ │
│  │  │                    PostgreSQL Database                          │  │ │
│  │  ├─────────────────────────────────────────────────────────────────┤  │ │
│  │  │  EXISTING TABLES:        NEW TABLES:                            │  │ │
│  │  │  • users                 • forum_categories                     │  │ │
│  │  │  • licenses              • forum_posts                          │  │ │
│  │  │  • license_usage         • forum_replies                        │  │ │
│  │  │                          • faq_questions                        │  │ │
│  │  │                          • faq_answers                          │  │ │
│  │  │                          • faq_votes                            │  │ │
│  │  │                          • donations                            │  │ │
│  │  │                          • products                             │  │ │
│  │  └─────────────────────────────────────────────────────────────────┘  │ │
│  │                                                                        │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                              │
│                                    │                                         │
│                                    ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                         STRIPE PAYMENT GATEWAY                         │ │
│  │                      (Donations & Future Purchases)                    │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Data Flow
```
1. User clicks "Community" in SAMPLAR
         │
         ▼
2. SAMPLAR retrieves auth token from LicenseSystemService
         │
         ▼
3. SAMPLAR loads WebBrowserComponent with URL:
   https://server.render.com/community?token={AUTH_TOKEN}
         │
         ▼
4. Server validates token via security_manager plugin
         │
         ▼
5. Server returns community HTML (user pre-authenticated)
         │
         ▼
6. User interacts with forum/FAQ/donations in WebView
         │
         ▼
7. All actions use same token for authentication
```

---

## 3. Server-Side Implementation

### 3.1 Plugin Structure (BIBLE.MD Compliant)
```
license_server/
├── plugins/
│   └── community_hub/
│       ├── __init__.py              # Plugin registration
│       ├── community_service.py     # Main service class
│       ├── forum_service.py         # Forum CRUD operations
│       ├── faq_service.py           # Q&A CRUD operations
│       ├── donation_service.py      # Stripe integration
│       ├── product_service.py       # Product showcase
│       ├── templates/
│       │   ├── base.html            # Base template with nav
│       │   ├── community_home.html  # Landing page
│       │   ├── forum/
│       │   │   ├── index.html       # Forum categories
│       │   │   ├── category.html    # Posts in category
│       │   │   ├── thread.html      # Single thread view
│       │   │   └── new_post.html    # Create post form
│       │   ├── faq/
│       │   │   ├── index.html       # FAQ list
│       │   │   ├── question.html    # Single question
│       │   │   └── ask.html         # Ask question form
│       │   ├── donate/
│       │   │   ├── index.html       # Donation page
│       │   │   ├── success.html     # Thank you page
│       │   │   └── cancel.html      # Cancelled page
│       │   └── products/
│       │       ├── index.html       # Product gallery
│       │       └── product.html     # Single product
│       └── static/
│           ├── css/
│           │   └── community.css    # Community styles
│           ├── js/
│           │   └── community.js     # Client-side JS
│           └── images/
│               └── products/        # Product images
```

### 3.2 Plugin Registration (__init__.py)
```python
"""
Community Hub Plugin - BIBLE.MD Compliant

Provides forum, FAQ, donations, and product showcase
for the SAMPLAR community.
"""

from .community_service import CommunityHubService

PLUGIN_NAME = "community_hub"


def register(app):
    """
    Register the Community Hub plugin with the license server.

    BIBLE.MD Pattern:
    1. Create service instance
    2. Register with app
    3. Register routes
    """

    # Create the community hub service
    service = CommunityHubService(app.event_bus, app.get_config())

    # Register with the app
    app.register_service(PLUGIN_NAME, service)

    # Register API routes with FastAPI
    app.event_bus.publish("api.register_routes", {
        "plugin": PLUGIN_NAME,
        "service": service,
        "routes": service.get_routes()
    })

    # Register template routes (HTML pages)
    app.event_bus.publish("api.register_routes", {
        "plugin": f"{PLUGIN_NAME}_templates",
        "service": service,
        "routes": service.get_template_routes()
    })
```

### 3.3 Main Service Class (community_service.py)
```python
"""
Community Hub Service - BIBLE.MD Compliant

Main service orchestrating forum, FAQ, donations, and products.
"""

import logging
from typing import Dict, Any
from fastapi import APIRouter, Request, Depends
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pathlib import Path

logger = logging.getLogger(__name__)


class CommunityHubService:
    """
    Community Hub Service - BIBLE.MD Compliant

    Coordinates all community features:
    - Forum (discussions)
    - FAQ (Q&A with voting)
    - Donations (Stripe)
    - Products (showcase)
    """

    name = "community_hub"

    def __init__(self, event_bus, config: Dict[str, Any]):
        self.event_bus = event_bus
        self.config = config
        self.router = APIRouter()
        self.template_router = APIRouter()

        # Setup templates
        template_path = Path(__file__).parent / "templates"
        self.templates = Jinja2Templates(directory=str(template_path))

        # Initialize sub-services
        self.forum = None
        self.faq = None
        self.donations = None
        self.products = None

        # Setup routes
        self._setup_api_routes()
        self._setup_template_routes()

        logger.info("🏠 CommunityHubService initialized")

    def initialise(self):
        """Service initialization hook (BIBLE.MD pattern)"""
        from .forum_service import ForumService
        from .faq_service import FAQService
        from .donation_service import DonationService
        from .product_service import ProductService

        self.forum = ForumService(self.event_bus, self.config)
        self.faq = FAQService(self.event_bus, self.config)
        self.donations = DonationService(self.event_bus, self.config)
        self.products = ProductService(self.event_bus, self.config)

        logger.info("🏠 CommunityHubService ready")

    def shutdown(self):
        """Service shutdown hook (BIBLE.MD pattern)"""
        logger.info("🏠 CommunityHubService shutting down")

    async def _get_current_user(self, request: Request) -> Dict[str, Any]:
        """
        Get current user from auth token.
        Reuses existing security_manager authentication.
        """
        token = request.query_params.get("token") or request.cookies.get("auth_token")

        if not token:
            return None

        # Use existing security_manager to validate token
        try:
            from core.app import LicenseServerApp
            app = LicenseServerApp.get_instance()
            security = app.services.get("security_manager")

            if security:
                user = await security.validate_session_token(token)
                return user
        except Exception as e:
            logger.error(f"Auth error: {e}")

        return None

    def _setup_api_routes(self):
        """Setup JSON API routes for AJAX calls"""

        # Forum API
        @self.router.get("/api/community/forum/categories")
        async def get_categories():
            return await self.forum.get_categories()

        @self.router.get("/api/community/forum/posts/{category_id}")
        async def get_posts(category_id: int):
            return await self.forum.get_posts(category_id)

        @self.router.post("/api/community/forum/posts")
        async def create_post(request: Request):
            user = await self._get_current_user(request)
            if not user:
                return {"error": "Authentication required"}
            data = await request.json()
            return await self.forum.create_post(user["id"], data)

        @self.router.post("/api/community/forum/replies")
        async def create_reply(request: Request):
            user = await self._get_current_user(request)
            if not user:
                return {"error": "Authentication required"}
            data = await request.json()
            return await self.forum.create_reply(user["id"], data)

        # FAQ API
        @self.router.get("/api/community/faq/questions")
        async def get_questions():
            return await self.faq.get_questions()

        @self.router.post("/api/community/faq/questions")
        async def create_question(request: Request):
            user = await self._get_current_user(request)
            if not user:
                return {"error": "Authentication required"}
            data = await request.json()
            return await self.faq.create_question(user["id"], data)

        @self.router.post("/api/community/faq/vote")
        async def vote_question(request: Request):
            user = await self._get_current_user(request)
            if not user:
                return {"error": "Authentication required"}
            data = await request.json()
            return await self.faq.vote(user["id"], data)

        # Donations API
        @self.router.post("/api/community/donate/create-checkout")
        async def create_checkout(request: Request):
            user = await self._get_current_user(request)
            if not user:
                return {"error": "Authentication required"}
            data = await request.json()
            return await self.donations.create_checkout_session(user["id"], data)

        @self.router.post("/api/community/donate/webhook")
        async def stripe_webhook(request: Request):
            return await self.donations.handle_webhook(request)

        # Products API
        @self.router.get("/api/community/products")
        async def get_products():
            return await self.products.get_all()

    def _setup_template_routes(self):
        """Setup HTML template routes"""

        @self.template_router.get("/community", response_class=HTMLResponse)
        async def community_home(request: Request):
            user = await self._get_current_user(request)
            return self.templates.TemplateResponse("community_home.html", {
                "request": request,
                "user": user,
                "page": "home"
            })

        @self.template_router.get("/community/forum", response_class=HTMLResponse)
        async def forum_index(request: Request):
            user = await self._get_current_user(request)
            categories = await self.forum.get_categories() if self.forum else []
            return self.templates.TemplateResponse("forum/index.html", {
                "request": request,
                "user": user,
                "categories": categories,
                "page": "forum"
            })

        @self.template_router.get("/community/forum/{category_id}", response_class=HTMLResponse)
        async def forum_category(request: Request, category_id: int):
            user = await self._get_current_user(request)
            category = await self.forum.get_category(category_id) if self.forum else None
            posts = await self.forum.get_posts(category_id) if self.forum else []
            return self.templates.TemplateResponse("forum/category.html", {
                "request": request,
                "user": user,
                "category": category,
                "posts": posts,
                "page": "forum"
            })

        @self.template_router.get("/community/forum/thread/{post_id}", response_class=HTMLResponse)
        async def forum_thread(request: Request, post_id: int):
            user = await self._get_current_user(request)
            post = await self.forum.get_post(post_id) if self.forum else None
            replies = await self.forum.get_replies(post_id) if self.forum else []
            return self.templates.TemplateResponse("forum/thread.html", {
                "request": request,
                "user": user,
                "post": post,
                "replies": replies,
                "page": "forum"
            })

        @self.template_router.get("/community/faq", response_class=HTMLResponse)
        async def faq_index(request: Request):
            user = await self._get_current_user(request)
            questions = await self.faq.get_questions() if self.faq else []
            return self.templates.TemplateResponse("faq/index.html", {
                "request": request,
                "user": user,
                "questions": questions,
                "page": "faq"
            })

        @self.template_router.get("/community/faq/{question_id}", response_class=HTMLResponse)
        async def faq_question(request: Request, question_id: int):
            user = await self._get_current_user(request)
            question = await self.faq.get_question(question_id) if self.faq else None
            answers = await self.faq.get_answers(question_id) if self.faq else []
            return self.templates.TemplateResponse("faq/question.html", {
                "request": request,
                "user": user,
                "question": question,
                "answers": answers,
                "page": "faq"
            })

        @self.template_router.get("/community/donate", response_class=HTMLResponse)
        async def donate_index(request: Request):
            user = await self._get_current_user(request)
            return self.templates.TemplateResponse("donate/index.html", {
                "request": request,
                "user": user,
                "page": "donate"
            })

        @self.template_router.get("/community/donate/success", response_class=HTMLResponse)
        async def donate_success(request: Request):
            user = await self._get_current_user(request)
            return self.templates.TemplateResponse("donate/success.html", {
                "request": request,
                "user": user,
                "page": "donate"
            })

        @self.template_router.get("/community/products", response_class=HTMLResponse)
        async def products_index(request: Request):
            user = await self._get_current_user(request)
            products = await self.products.get_all() if self.products else []
            return self.templates.TemplateResponse("products/index.html", {
                "request": request,
                "user": user,
                "products": products,
                "page": "products"
            })

    def get_routes(self) -> APIRouter:
        """Get API routes (BIBLE.MD pattern)"""
        return self.router

    def get_template_routes(self) -> APIRouter:
        """Get template routes"""
        return self.template_router
```

### 3.4 Forum Service (forum_service.py)
```python
"""
Forum Service - Handles forum CRUD operations
"""

import logging
from typing import Dict, Any, List, Optional
from datetime import datetime

logger = logging.getLogger(__name__)


class ForumService:
    """Forum CRUD operations"""

    def __init__(self, event_bus, config: Dict[str, Any]):
        self.event_bus = event_bus
        self.config = config

    def _get_db(self):
        """Get database service"""
        try:
            from core.app import LicenseServerApp
            app = LicenseServerApp.get_instance()
            return app.services.get("database_manager")
        except:
            return None

    async def get_categories(self) -> List[Dict[str, Any]]:
        """Get all forum categories"""
        db = self._get_db()
        if not db:
            return []

        try:
            return await db.execute_query(
                """
                SELECT c.*, COUNT(p.id) as post_count
                FROM forum_categories c
                LEFT JOIN forum_posts p ON p.category_id = c.id
                GROUP BY c.id
                ORDER BY c.sort_order, c.name
                """
            )
        except Exception as e:
            logger.error(f"Error getting categories: {e}")
            return []

    async def get_category(self, category_id: int) -> Optional[Dict[str, Any]]:
        """Get single category"""
        db = self._get_db()
        if not db:
            return None

        try:
            results = await db.execute_query(
                "SELECT * FROM forum_categories WHERE id = %s",
                (category_id,)
            )
            return results[0] if results else None
        except Exception as e:
            logger.error(f"Error getting category: {e}")
            return None

    async def get_posts(self, category_id: int, limit: int = 50) -> List[Dict[str, Any]]:
        """Get posts in a category"""
        db = self._get_db()
        if not db:
            return []

        try:
            return await db.execute_query(
                """
                SELECT p.*, u.email as author_email,
                       COUNT(r.id) as reply_count,
                       MAX(COALESCE(r.created_at, p.created_at)) as last_activity
                FROM forum_posts p
                JOIN users u ON u.id = p.user_id
                LEFT JOIN forum_replies r ON r.post_id = p.id
                WHERE p.category_id = %s
                GROUP BY p.id, u.email
                ORDER BY p.is_pinned DESC, last_activity DESC
                LIMIT %s
                """,
                (category_id, limit)
            )
        except Exception as e:
            logger.error(f"Error getting posts: {e}")
            return []

    async def get_post(self, post_id: int) -> Optional[Dict[str, Any]]:
        """Get single post with author info"""
        db = self._get_db()
        if not db:
            return None

        try:
            results = await db.execute_query(
                """
                SELECT p.*, u.email as author_email
                FROM forum_posts p
                JOIN users u ON u.id = p.user_id
                WHERE p.id = %s
                """,
                (post_id,)
            )

            if results:
                # Increment view count
                await db.execute_query(
                    "UPDATE forum_posts SET view_count = view_count + 1 WHERE id = %s",
                    (post_id,)
                )
                return results[0]
            return None
        except Exception as e:
            logger.error(f"Error getting post: {e}")
            return None

    async def get_replies(self, post_id: int) -> List[Dict[str, Any]]:
        """Get replies to a post"""
        db = self._get_db()
        if not db:
            return []

        try:
            return await db.execute_query(
                """
                SELECT r.*, u.email as author_email
                FROM forum_replies r
                JOIN users u ON u.id = r.user_id
                WHERE r.post_id = %s
                ORDER BY r.created_at ASC
                """,
                (post_id,)
            )
        except Exception as e:
            logger.error(f"Error getting replies: {e}")
            return []

    async def create_post(self, user_id: int, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create a new forum post"""
        db = self._get_db()
        if not db:
            return {"error": "Database unavailable"}

        try:
            result = await db.execute_query(
                """
                INSERT INTO forum_posts (category_id, user_id, title, content, created_at)
                VALUES (%s, %s, %s, %s, %s)
                RETURNING id
                """,
                (
                    data.get("category_id"),
                    user_id,
                    data.get("title"),
                    data.get("content"),
                    datetime.now().isoformat()
                )
            )
            return {"success": True, "post_id": result[0]["id"]}
        except Exception as e:
            logger.error(f"Error creating post: {e}")
            return {"error": str(e)}

    async def create_reply(self, user_id: int, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create a reply to a post"""
        db = self._get_db()
        if not db:
            return {"error": "Database unavailable"}

        try:
            result = await db.execute_query(
                """
                INSERT INTO forum_replies (post_id, user_id, content, created_at)
                VALUES (%s, %s, %s, %s)
                RETURNING id
                """,
                (
                    data.get("post_id"),
                    user_id,
                    data.get("content"),
                    datetime.now().isoformat()
                )
            )
            return {"success": True, "reply_id": result[0]["id"]}
        except Exception as e:
            logger.error(f"Error creating reply: {e}")
            return {"error": str(e)}
```

### 3.5 FAQ Service (faq_service.py)
```python
"""
FAQ Service - Handles Q&A with voting
"""

import logging
from typing import Dict, Any, List, Optional
from datetime import datetime

logger = logging.getLogger(__name__)


class FAQService:
    """FAQ/Q&A CRUD operations with voting"""

    def __init__(self, event_bus, config: Dict[str, Any]):
        self.event_bus = event_bus
        self.config = config

    def _get_db(self):
        """Get database service"""
        try:
            from core.app import LicenseServerApp
            app = LicenseServerApp.get_instance()
            return app.services.get("database_manager")
        except:
            return None

    async def get_questions(self, limit: int = 50) -> List[Dict[str, Any]]:
        """Get questions sorted by votes"""
        db = self._get_db()
        if not db:
            return []

        try:
            return await db.execute_query(
                """
                SELECT q.*, u.email as author_email,
                       COUNT(DISTINCT a.id) as answer_count,
                       COALESCE(SUM(v.vote_value), 0) as vote_score
                FROM faq_questions q
                JOIN users u ON u.id = q.user_id
                LEFT JOIN faq_answers a ON a.question_id = q.id
                LEFT JOIN faq_votes v ON v.question_id = q.id
                GROUP BY q.id, u.email
                ORDER BY vote_score DESC, q.created_at DESC
                LIMIT %s
                """,
                (limit,)
            )
        except Exception as e:
            logger.error(f"Error getting questions: {e}")
            return []

    async def get_question(self, question_id: int) -> Optional[Dict[str, Any]]:
        """Get single question"""
        db = self._get_db()
        if not db:
            return None

        try:
            results = await db.execute_query(
                """
                SELECT q.*, u.email as author_email,
                       COALESCE(SUM(v.vote_value), 0) as vote_score
                FROM faq_questions q
                JOIN users u ON u.id = q.user_id
                LEFT JOIN faq_votes v ON v.question_id = q.id
                WHERE q.id = %s
                GROUP BY q.id, u.email
                """,
                (question_id,)
            )

            if results:
                # Increment view count
                await db.execute_query(
                    "UPDATE faq_questions SET view_count = view_count + 1 WHERE id = %s",
                    (question_id,)
                )
                return results[0]
            return None
        except Exception as e:
            logger.error(f"Error getting question: {e}")
            return None

    async def get_answers(self, question_id: int) -> List[Dict[str, Any]]:
        """Get answers to a question"""
        db = self._get_db()
        if not db:
            return []

        try:
            return await db.execute_query(
                """
                SELECT a.*, u.email as author_email
                FROM faq_answers a
                JOIN users u ON u.id = a.user_id
                WHERE a.question_id = %s
                ORDER BY a.is_accepted DESC, a.created_at ASC
                """,
                (question_id,)
            )
        except Exception as e:
            logger.error(f"Error getting answers: {e}")
            return []

    async def create_question(self, user_id: int, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create a new question"""
        db = self._get_db()
        if not db:
            return {"error": "Database unavailable"}

        try:
            result = await db.execute_query(
                """
                INSERT INTO faq_questions (user_id, title, content, tags, created_at)
                VALUES (%s, %s, %s, %s, %s)
                RETURNING id
                """,
                (
                    user_id,
                    data.get("title"),
                    data.get("content"),
                    data.get("tags", ""),
                    datetime.now().isoformat()
                )
            )
            return {"success": True, "question_id": result[0]["id"]}
        except Exception as e:
            logger.error(f"Error creating question: {e}")
            return {"error": str(e)}

    async def create_answer(self, user_id: int, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create an answer"""
        db = self._get_db()
        if not db:
            return {"error": "Database unavailable"}

        try:
            result = await db.execute_query(
                """
                INSERT INTO faq_answers (question_id, user_id, content, created_at)
                VALUES (%s, %s, %s, %s)
                RETURNING id
                """,
                (
                    data.get("question_id"),
                    user_id,
                    data.get("content"),
                    datetime.now().isoformat()
                )
            )
            return {"success": True, "answer_id": result[0]["id"]}
        except Exception as e:
            logger.error(f"Error creating answer: {e}")
            return {"error": str(e)}

    async def vote(self, user_id: int, data: Dict[str, Any]) -> Dict[str, Any]:
        """Vote on a question (upvote/downvote)"""
        db = self._get_db()
        if not db:
            return {"error": "Database unavailable"}

        question_id = data.get("question_id")
        vote_value = data.get("vote_value", 1)  # 1 = upvote, -1 = downvote

        try:
            # Check if user already voted
            existing = await db.execute_query(
                "SELECT id, vote_value FROM faq_votes WHERE question_id = %s AND user_id = %s",
                (question_id, user_id)
            )

            if existing:
                if existing[0]["vote_value"] == vote_value:
                    # Remove vote if same value
                    await db.execute_query(
                        "DELETE FROM faq_votes WHERE id = %s",
                        (existing[0]["id"],)
                    )
                    return {"success": True, "action": "removed"}
                else:
                    # Update vote
                    await db.execute_query(
                        "UPDATE faq_votes SET vote_value = %s WHERE id = %s",
                        (vote_value, existing[0]["id"])
                    )
                    return {"success": True, "action": "changed"}
            else:
                # Create new vote
                await db.execute_query(
                    """
                    INSERT INTO faq_votes (question_id, user_id, vote_value, created_at)
                    VALUES (%s, %s, %s, %s)
                    """,
                    (question_id, user_id, vote_value, datetime.now().isoformat())
                )
                return {"success": True, "action": "created"}
        except Exception as e:
            logger.error(f"Error voting: {e}")
            return {"error": str(e)}
```

### 3.6 Donation Service (donation_service.py)
```python
"""
Donation Service - Stripe integration for donations
"""

import logging
import os
from typing import Dict, Any
from datetime import datetime

logger = logging.getLogger(__name__)

# Stripe configuration
STRIPE_SECRET_KEY = os.environ.get("STRIPE_SECRET_KEY", "")
STRIPE_WEBHOOK_SECRET = os.environ.get("STRIPE_WEBHOOK_SECRET", "")
STRIPE_PUBLIC_KEY = os.environ.get("STRIPE_PUBLIC_KEY", "")


class DonationService:
    """Stripe donation handling"""

    def __init__(self, event_bus, config: Dict[str, Any]):
        self.event_bus = event_bus
        self.config = config
        self.stripe = None

        # Initialize Stripe
        if STRIPE_SECRET_KEY:
            try:
                import stripe
                stripe.api_key = STRIPE_SECRET_KEY
                self.stripe = stripe
                logger.info("💳 Stripe initialized")
            except ImportError:
                logger.warning("⚠️ Stripe library not installed")

    def _get_db(self):
        """Get database service"""
        try:
            from core.app import LicenseServerApp
            app = LicenseServerApp.get_instance()
            return app.services.get("database_manager")
        except:
            return None

    async def create_checkout_session(self, user_id: int, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create Stripe checkout session for donation"""
        if not self.stripe:
            return {"error": "Stripe not configured"}

        amount = data.get("amount", 500)  # Default $5.00 (in cents)

        # Get base URL from config
        base_url = self.config.get("server", {}).get("base_url", "http://localhost:8000")

        try:
            session = self.stripe.checkout.Session.create(
                payment_method_types=["card"],
                line_items=[{
                    "price_data": {
                        "currency": "usd",
                        "product_data": {
                            "name": "SAMPLAR Donation",
                            "description": "Support SAMPLAR development",
                        },
                        "unit_amount": amount,
                    },
                    "quantity": 1,
                }],
                mode="payment",
                success_url=f"{base_url}/community/donate/success?session_id={{CHECKOUT_SESSION_ID}}",
                cancel_url=f"{base_url}/community/donate?cancelled=true",
                metadata={
                    "user_id": str(user_id),
                    "type": "donation"
                }
            )

            return {
                "success": True,
                "checkout_url": session.url,
                "session_id": session.id
            }
        except Exception as e:
            logger.error(f"Error creating checkout: {e}")
            return {"error": str(e)}

    async def handle_webhook(self, request) -> Dict[str, Any]:
        """Handle Stripe webhook events"""
        if not self.stripe:
            return {"error": "Stripe not configured"}

        try:
            payload = await request.body()
            sig_header = request.headers.get("stripe-signature")

            event = self.stripe.Webhook.construct_event(
                payload, sig_header, STRIPE_WEBHOOK_SECRET
            )

            if event["type"] == "checkout.session.completed":
                session = event["data"]["object"]
                await self._record_donation(session)

            return {"success": True}
        except Exception as e:
            logger.error(f"Webhook error: {e}")
            return {"error": str(e)}

    async def _record_donation(self, session: Dict[str, Any]):
        """Record successful donation in database"""
        db = self._get_db()
        if not db:
            return

        try:
            user_id = session.get("metadata", {}).get("user_id")
            amount = session.get("amount_total", 0)

            await db.execute_query(
                """
                INSERT INTO donations (user_id, amount_cents, stripe_session_id,
                                       stripe_payment_intent, status, created_at)
                VALUES (%s, %s, %s, %s, %s, %s)
                """,
                (
                    user_id,
                    amount,
                    session.get("id"),
                    session.get("payment_intent"),
                    "completed",
                    datetime.now().isoformat()
                )
            )
            logger.info(f"💳 Donation recorded: ${amount/100:.2f} from user {user_id}")
        except Exception as e:
            logger.error(f"Error recording donation: {e}")

    async def get_donation_stats(self) -> Dict[str, Any]:
        """Get donation statistics"""
        db = self._get_db()
        if not db:
            return {}

        try:
            result = await db.execute_query(
                """
                SELECT COUNT(*) as total_donations,
                       SUM(amount_cents) as total_amount,
                       COUNT(DISTINCT user_id) as unique_donors
                FROM donations
                WHERE status = 'completed'
                """
            )

            if result:
                return {
                    "total_donations": result[0]["total_donations"],
                    "total_amount": result[0]["total_amount"] / 100,  # Convert to dollars
                    "unique_donors": result[0]["unique_donors"]
                }
            return {}
        except Exception as e:
            logger.error(f"Error getting stats: {e}")
            return {}
```

### 3.7 Product Service (product_service.py)
```python
"""
Product Service - Product showcase for cross-promotion
"""

import logging
from typing import Dict, Any, List, Optional

logger = logging.getLogger(__name__)


class ProductService:
    """Product showcase management"""

    def __init__(self, event_bus, config: Dict[str, Any]):
        self.event_bus = event_bus
        self.config = config

    def _get_db(self):
        """Get database service"""
        try:
            from core.app import LicenseServerApp
            app = LicenseServerApp.get_instance()
            return app.services.get("database_manager")
        except:
            return None

    async def get_all(self) -> List[Dict[str, Any]]:
        """Get all products"""
        db = self._get_db()
        if not db:
            # Return static products if DB unavailable
            return self._get_static_products()

        try:
            return await db.execute_query(
                """
                SELECT * FROM products
                WHERE is_active = true
                ORDER BY sort_order, name
                """
            )
        except Exception as e:
            logger.error(f"Error getting products: {e}")
            return self._get_static_products()

    async def get_product(self, product_id: int) -> Optional[Dict[str, Any]]:
        """Get single product"""
        db = self._get_db()
        if not db:
            return None

        try:
            results = await db.execute_query(
                "SELECT * FROM products WHERE id = %s AND is_active = true",
                (product_id,)
            )
            return results[0] if results else None
        except Exception as e:
            logger.error(f"Error getting product: {e}")
            return None

    def _get_static_products(self) -> List[Dict[str, Any]]:
        """Static product list as fallback"""
        return [
            {
                "id": 1,
                "name": "Varia",
                "tagline": "Advanced Sound Variation Engine",
                "description": "Create entirely new sounds by combining and morphing samples with cutting-edge algorithms.",
                "price": 49.99,
                "image_url": "/static/images/products/varia.png",
                "purchase_url": "https://yoursite.com/varia",
                "features": [
                    "Unique sound-combining engines",
                    "Advanced morphing algorithms",
                    "Professional workflow"
                ]
            },
            {
                "id": 2,
                "name": "SAMPLAR",
                "tagline": "The Ultimate Drum Sample Tool",
                "description": "Process, vary, and organize your drum samples with ease.",
                "price": 0,
                "image_url": "/static/images/products/SAMPLAR.png",
                "purchase_url": "https://yoursite.com/SAMPLAR",
                "features": [
                    "8 variation engines",
                    "Step sequencer",
                    "Demucs separation",
                    "FREE & Open Source"
                ]
            }
        ]
```

---

## 4. Client-Side Implementation (SAMPLAR)

### 4.1 Overview
SAMPLAR needs minimal changes - just a new page containing a WebBrowserComponent.

### 4.2 File Structure
```
Sample_monsta5/
├── src/
│   └── ui/
│       └── Pages/
│           └── CommunityPage.h      # NEW
│           └── CommunityPage.cpp    # NEW
```

### 4.3 CommunityPage.h
```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace samplar {

/**
 * CommunityPage - Displays the community hub website in an embedded browser.
 *
 * Features:
 * - Embedded WebBrowserComponent showing community website
 * - Automatic authentication via license token
 * - Refresh button for manual reload
 * - Loading indicator
 */
class CommunityPage : public juce::Component {
public:
    CommunityPage();
    ~CommunityPage() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Load the community page with authentication */
    void loadCommunityPage();

    /** Refresh the current page */
    void refresh();

    /** Set the base URL for the community server */
    void setServerUrl(const juce::String& url);

private:
    // UI Components
    std::unique_ptr<juce::WebBrowserComponent> browser_;
    juce::TextButton refreshButton_{ "Refresh" };
    juce::Label statusLabel_;

    // Configuration
    juce::String serverBaseUrl_ = "https://your-server.render.com";
    juce::String authToken_;
    bool isLoading_ = false;

    /** Get authentication token from license system */
    juce::String getAuthToken();

    /** Build the full community URL with auth token */
    juce::String buildCommunityUrl();

    /** Handle browser page load events */
    void onPageLoaded(bool success);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommunityPage)
};

} // namespace samplar
```

### 4.4 CommunityPage.cpp
```cpp
#include "CommunityPage.h"
#include "../../services/LicenseSystemService.h"
#include "../../app/ServiceRegistry.h"

namespace samplar {

CommunityPage::CommunityPage()
{
    juce::Logger::writeToLog("[CommunityPage] === CONSTRUCTOR START ===");

    // Create WebBrowserComponent
    browser_ = std::make_unique<juce::WebBrowserComponent>(
        juce::WebBrowserComponent::Options()
            .withBackend(juce::WebBrowserComponent::Options::Backend::defaultBackend)
            .withNativeIntegrationEnabled()
    );
    addAndMakeVisible(*browser_);

    // Setup refresh button
    refreshButton_.onClick = [this]() { refresh(); };
    addAndMakeVisible(refreshButton_);

    // Setup status label
    statusLabel_.setText("Loading community...", juce::dontSendNotification);
    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(statusLabel_);

    juce::Logger::writeToLog("[CommunityPage] === CONSTRUCTOR END ===");
}

CommunityPage::~CommunityPage()
{
    juce::Logger::writeToLog("[CommunityPage] Destructor called");
}

void CommunityPage::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));  // Dark background
}

void CommunityPage::resized()
{
    auto bounds = getLocalBounds();

    // Top bar with refresh button
    auto topBar = bounds.removeFromTop(30);
    refreshButton_.setBounds(topBar.removeFromRight(80).reduced(4));
    statusLabel_.setBounds(topBar.reduced(4));

    // Browser fills the rest
    browser_->setBounds(bounds);
}

void CommunityPage::loadCommunityPage()
{
    juce::Logger::writeToLog("[CommunityPage] === loadCommunityPage START ===");

    // Get auth token
    authToken_ = getAuthToken();
    juce::Logger::writeToLog("[CommunityPage] Auth token obtained: " +
        (authToken_.isEmpty() ? "NONE" : authToken_.substring(0, 10) + "..."));

    // Build URL and load
    juce::String url = buildCommunityUrl();
    juce::Logger::writeToLog("[CommunityPage] Loading URL: " + url);

    isLoading_ = true;
    statusLabel_.setText("Loading community...", juce::dontSendNotification);

    browser_->goToURL(url);

    juce::Logger::writeToLog("[CommunityPage] === loadCommunityPage END ===");
}

void CommunityPage::refresh()
{
    juce::Logger::writeToLog("[CommunityPage] Refresh requested");
    loadCommunityPage();
}

void CommunityPage::setServerUrl(const juce::String& url)
{
    serverBaseUrl_ = url;
    juce::Logger::writeToLog("[CommunityPage] Server URL set to: " + url);
}

juce::String CommunityPage::getAuthToken()
{
    juce::Logger::writeToLog("[CommunityPage] Getting auth token from LicenseSystemService");

    try {
        // Get the license service from registry
        auto* registry = ServiceRegistry::getInstance();
        if (!registry) {
            juce::Logger::writeToLog("[CommunityPage] ServiceRegistry not available");
            return {};
        }

        auto* licenseService = dynamic_cast<LicenseSystemService*>(
            registry->get("license_system")
        );

        if (!licenseService) {
            juce::Logger::writeToLog("[CommunityPage] LicenseSystemService not available");
            return {};
        }

        // Get the session token from the license service
        juce::String token = licenseService->getSessionToken();

        if (token.isEmpty()) {
            juce::Logger::writeToLog("[CommunityPage] No session token available");
        }

        return token;
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog("[CommunityPage] Error getting token: " + juce::String(e.what()));
        return {};
    }
}

juce::String CommunityPage::buildCommunityUrl()
{
    juce::String url = serverBaseUrl_ + "/community";

    if (authToken_.isNotEmpty()) {
        url += "?token=" + juce::URL::addEscapeChars(authToken_, true);
    }

    return url;
}

void CommunityPage::onPageLoaded(bool success)
{
    isLoading_ = false;

    if (success) {
        statusLabel_.setText("Connected to community", juce::dontSendNotification);
        juce::Logger::writeToLog("[CommunityPage] Page loaded successfully");
    } else {
        statusLabel_.setText("Failed to load - click Refresh to retry", juce::dontSendNotification);
        juce::Logger::writeToLog("[CommunityPage] Page load failed");
    }
}

} // namespace samplar
```

### 4.5 Integration with MainWindow

Add to navigation in `MainWindow.cpp`:

```cpp
// In MainWindow constructor or page setup:
#include "Pages/CommunityPage.h"

// Create community page
communityPage_ = std::make_unique<CommunityPage>();
communityPage_->setServerUrl("https://your-license-server.render.com");

// Add to page stack/navigation
pageStack_.addChildComponent(*communityPage_);

// Add navigation button
navButtons_.add(new juce::TextButton("Community"));
navButtons_.getLast()->onClick = [this]() {
    showPage("community");
    communityPage_->loadCommunityPage();  // Load when shown
};
```

### 4.6 CMakeLists.txt Addition

```cmake
# Add to SM5App sources
target_sources(SM5App PRIVATE
    # ... existing sources ...
    src/ui/Pages/CommunityPage.h
    src/ui/Pages/CommunityPage.cpp
)

# Ensure JUCE WebBrowserComponent module is enabled
target_compile_definitions(SM5App PRIVATE
    JUCE_WEB_BROWSER=1
)
```

### 4.7 Community Slides Carousel (Initial Page Banner)

The Initial Page features an auto-scrolling image carousel banner that promotes community content, new features, sample packs, and announcements.

#### 4.7.1 Visual Design

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           INITIAL PAGE LAYOUT                                 │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│    ┌────────────────────────────────────────────────────────────────────┐   │
│    │  ╔════════════════════════════════════════════════════════════╗    │   │
│    │  ║                    [SLIDE IMAGE/GRAPHIC]                   ║    │   │
│    │  ║                                                            ║    │   │
│    │  ║  HEADLINE TEXT                                    [OPEN]   ║    │   │
│    │  ║  Subtitle or description text                              ║    │   │
│    │  ║                                                            ║    │   │
│    │  ║                      o  O  o  o  o                         ║    │   │
│    │  ╚════════════════════════════════════════════════════════════╝    │   │
│    └────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│                     ┌─────────────────────────────────────┐                  │
│                     │   Drop your audio file or click...   │                  │
│                     └─────────────────────────────────────┘                  │
│                                    ...                                        │
└──────────────────────────────────────────────────────────────────────────────┘

Legend:
- [SLIDE IMAGE/GRAPHIC] = Background image or gradient for current slide
- HEADLINE TEXT = Bold title (e.g., "New Sample Packs Available!")
- Subtitle = Description text
- [OPEN] = Action button (opens Community Hub page or specific URL)
- o O o o o = Navigation dots (O = current slide, o = other slides)
```

#### 4.7.2 Banner Dimensions

| Element | Value | Notes |
|---------|-------|-------|
| **Banner Height** | 120px | Fixed height (compact design for balanced layout) |
| **Banner Max Width** | 720px | Responsive, horizontally centered |
| **Banner Min Margin** | 16px | Left/right padding from window edge |
| **Y Position** | 50px | Below window title bar area |
| **Corner Radius** | 6px | Rounded corners |
| **Border Width** | 1.5px | Cyan border (#00FFFF) |
| **Arrow Hit Area** | 28px wide | Left/right navigation zones (full height) |
| **Arrow Size** | 10px | Chevron arrow dimensions |
| **Nav Dot Size** | 7px | Diameter of each dot |
| **Nav Dot Spacing** | 14px | Center-to-center distance |
| **Nav Dot Y Offset** | 14px | From bottom of banner |
| **Text Padding** | 18px horizontal, 12px vertical | Internal content margins |
| **Title Font** | 20px bold | Gold color (#FFD700), vertically centered |
| **Subtitle Font** | 13px regular | Light grey |
| **Text Gap** | 4px | Vertical space between title and subtitle |

**Recommended Image Dimensions:**
- **Aspect Ratio**: 6:1 (720 x 120)
- **Minimum Size**: 720 x 120px (for crisp display at max size)
- **File Format**: PNG (with transparency) or JPG
- **Content Safe Area**: Keep important content 28px from left/right edges (arrow zones)

#### 4.7.3 Slide Data Structure

Each slide contains:

| Field | Type | Description |
|-------|------|-------------|
| `image` | juce::Image | Background image (loaded from file or embedded) |
| `title` | juce::String | Bold headline text (max 40 chars recommended) |
| `subtitle` | juce::String | Description text (max 80 chars recommended) |
| `url` | juce::String | Optional: Specific URL when clicked (default: Community Hub) |
| `buttonText` | juce::String | Optional: Custom button text (default: "OPEN") |
| `titleColor` | juce::Colour | Optional: Custom title color (default: Gold #FFD700) |
| `subtitleColor` | juce::Colour | Optional: Custom subtitle color (default: Grey) |

#### 4.7.4 Header File (InitialPage.h - CommunityBanner class)

```cpp
// Community Hub banner carousel at top of page
class CommunityBanner : public juce::Component, private juce::Timer
{
public:
    // Slide data structure
    struct Slide {
        juce::Image image;
        juce::String title;
        juce::String subtitle;
        juce::String url;           // Optional: custom URL for this slide
        juce::String buttonText { "OPEN" };
        juce::Colour titleColor { 0xFFFFD700 };     // Gold
        juce::Colour subtitleColor { 0xFF888888 };  // Grey
    };

    CommunityBanner();
    ~CommunityBanner() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Mouse interaction
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Slide management
    void addSlide(const Slide& slide);
    void clearSlides();
    void loadSlidesFromDirectory(const juce::File& slidesDir);
    void loadSlidesFromJson(const juce::File& jsonFile);

    // Configuration
    void setAutoScrollInterval(int milliseconds);  // Default: 5000ms
    void setTransitionDuration(int milliseconds);  // Default: 300ms
    void setPauseOnHover(bool pause);              // Default: true
    void setShowNavigationDots(bool show);         // Default: true

    // Manual navigation
    void goToSlide(int index);
    void nextSlide();
    void previousSlide();

    // Callback when slide action is triggered
    std::function<void()> onClick;                     // Default action
    std::function<void(const juce::String&)> onSlideClick;  // URL-specific action

private:
    // Slides
    std::vector<Slide> slides_;
    int currentSlideIndex_ { 0 };
    int previousSlideIndex_ { -1 };

    // Auto-scroll settings
    int autoScrollMs_ { 5000 };
    int transitionMs_ { 300 };
    bool pauseOnHover_ { true };
    bool showDots_ { true };

    // Animation state
    float transitionAlpha_ { 1.0f };
    bool isTransitioning_ { false };
    bool hovering_ { false };

    // UI Components
    juce::TextButton openButton_ { "OPEN" };

    // Internal methods
    void startTransition(int targetSlide);
    void loadDefaultSlides();
    void drawSlide(juce::Graphics& g, const Slide& slide, float alpha);
    void drawNavigationDots(juce::Graphics& g);
    int getClickedDotIndex(const juce::Point<int>& position) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommunityBanner)
};
```

#### 4.7.5 Implementation File (InitialPage.cpp - CommunityBanner)

```cpp
//==============================================================================
// CommunityBanner Implementation (Carousel)
//==============================================================================

InitialPage::CommunityBanner::CommunityBanner()
{
    // Style the action button
    openButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF00FFFF));
    openButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF0D1117));
    openButton_.onClick = [this] {
        if (!slides_.empty() && !slides_[currentSlideIndex_].url.isEmpty()) {
            if (onSlideClick) onSlideClick(slides_[currentSlideIndex_].url);
        } else {
            if (onClick) onClick();
        }
    };
    addAndMakeVisible(openButton_);

    // Load default slides or from directory
    loadDefaultSlides();

    // Start auto-scroll timer
    startTimerHz(60);  // 60fps for smooth animation
}

InitialPage::CommunityBanner::~CommunityBanner()
{
    stopTimer();
}

void InitialPage::CommunityBanner::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Draw dark background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(bounds, 6.0f);

    if (slides_.empty())
    {
        // No slides - show placeholder
        g.setColour(juce::Colour(0xFFFFD700));
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText("COMMUNITY HUB", bounds, juce::Justification::centred);
    }
    else
    {
        // Draw current slide (with transition if active)
        if (isTransitioning_ && previousSlideIndex_ >= 0)
        {
            // Fade out previous slide
            drawSlide(g, slides_[previousSlideIndex_], 1.0f - transitionAlpha_);
            // Fade in current slide
            drawSlide(g, slides_[currentSlideIndex_], transitionAlpha_);
        }
        else
        {
            drawSlide(g, slides_[currentSlideIndex_], 1.0f);
        }

        // Draw navigation dots
        if (showDots_ && slides_.size() > 1)
        {
            drawNavigationDots(g);
        }
    }

    // Border - brighter on hover
    float borderAlpha = hovering_ ? 0.8f : 0.4f;
    g.setColour(juce::Colour(0xFF00FFFF).withAlpha(borderAlpha));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 6.0f, 1.5f);
}

void InitialPage::CommunityBanner::drawSlide(juce::Graphics& g, const Slide& slide, float alpha)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Draw background image if available
    if (slide.image.isValid())
    {
        g.setOpacity(alpha * 0.7f);  // Semi-transparent for text readability
        g.drawImage(slide.image, bounds, juce::RectanglePlacement::fillDestination);
        g.setOpacity(1.0f);

        // Dark overlay for text contrast
        g.setColour(juce::Colours::black.withAlpha(alpha * 0.5f));
        g.fillRoundedRectangle(bounds, 4.0f);
    }

    // Draw title
    auto textArea = bounds.reduced(12.0f, 8.0f);
    g.setColour(slide.titleColor.withAlpha(alpha));
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText(slide.title, textArea.removeFromTop(22.0f), juce::Justification::centredLeft);

    // Draw subtitle
    g.setColour(slide.subtitleColor.withAlpha(alpha));
    g.setFont(juce::Font(12.0f));
    g.drawText(slide.subtitle, textArea.removeFromTop(16.0f), juce::Justification::centredLeft);
}

void InitialPage::CommunityBanner::drawNavigationDots(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    int numDots = (int)slides_.size();
    int dotSize = 8;
    int dotGap = 6;
    int totalWidth = numDots * dotSize + (numDots - 1) * dotGap;
    int startX = (bounds.getWidth() - totalWidth) / 2;
    int y = bounds.getBottom() - 16;

    for (int i = 0; i < numDots; ++i)
    {
        int x = startX + i * (dotSize + dotGap);
        bool isCurrent = (i == currentSlideIndex_);

        if (isCurrent)
        {
            g.setColour(juce::Colour(0xFF00FFFF));  // Cyan for current
            g.fillEllipse((float)x, (float)y, (float)dotSize, (float)dotSize);
        }
        else
        {
            g.setColour(juce::Colour(0xFF666666));  // Grey for others
            g.fillEllipse((float)x, (float)y, (float)dotSize, (float)dotSize);
        }
    }
}

void InitialPage::CommunityBanner::resized()
{
    auto bounds = getLocalBounds();
    // OPEN button on right side
    openButton_.setBounds(bounds.removeFromRight(70).reduced(8, 10));
}

void InitialPage::CommunityBanner::timerCallback()
{
    // Handle transition animation
    if (isTransitioning_)
    {
        transitionAlpha_ += 1.0f / (transitionMs_ / 16.67f);  // 60fps steps
        if (transitionAlpha_ >= 1.0f)
        {
            transitionAlpha_ = 1.0f;
            isTransitioning_ = false;
            previousSlideIndex_ = -1;
        }
        repaint();
        return;
    }

    // Auto-scroll (only if not hovering and not transitioning)
    if (!hovering_ && !slides_.empty() && slides_.size() > 1)
    {
        static int frameCount = 0;
        frameCount++;

        int framesPerScroll = (autoScrollMs_ * 60) / 1000;  // Convert ms to frames at 60fps
        if (frameCount >= framesPerScroll)
        {
            frameCount = 0;
            nextSlide();
        }
    }
}

void InitialPage::CommunityBanner::mouseEnter(const juce::MouseEvent&)
{
    hovering_ = true;
    repaint();
}

void InitialPage::CommunityBanner::mouseExit(const juce::MouseEvent&)
{
    hovering_ = false;
    repaint();
}

void InitialPage::CommunityBanner::mouseUp(const juce::MouseEvent& e)
{
    // Check if clicked on navigation dot
    if (showDots_ && slides_.size() > 1)
    {
        int clickedDot = getClickedDotIndex(e.getPosition());
        if (clickedDot >= 0 && clickedDot != currentSlideIndex_)
        {
            goToSlide(clickedDot);
            return;
        }
    }

    // Otherwise trigger default action
    if (getLocalBounds().contains(e.getPosition()))
    {
        if (!slides_.empty() && !slides_[currentSlideIndex_].url.isEmpty())
        {
            if (onSlideClick) onSlideClick(slides_[currentSlideIndex_].url);
        }
        else
        {
            if (onClick) onClick();
        }
    }
}

int InitialPage::CommunityBanner::getClickedDotIndex(const juce::Point<int>& position) const
{
    auto bounds = getLocalBounds();
    int numDots = (int)slides_.size();
    int dotSize = 8;
    int dotGap = 6;
    int totalWidth = numDots * dotSize + (numDots - 1) * dotGap;
    int startX = (bounds.getWidth() - totalWidth) / 2;
    int y = bounds.getBottom() - 16;

    for (int i = 0; i < numDots; ++i)
    {
        int x = startX + i * (dotSize + dotGap);
        juce::Rectangle<int> dotBounds(x - 4, y - 4, dotSize + 8, dotSize + 8);  // Larger hit area
        if (dotBounds.contains(position))
            return i;
    }
    return -1;
}

void InitialPage::CommunityBanner::startTransition(int targetSlide)
{
    if (targetSlide == currentSlideIndex_ || slides_.empty())
        return;

    previousSlideIndex_ = currentSlideIndex_;
    currentSlideIndex_ = targetSlide;
    transitionAlpha_ = 0.0f;
    isTransitioning_ = true;

    // Update button text for new slide
    if (!slides_[currentSlideIndex_].buttonText.isEmpty())
        openButton_.setButtonText(slides_[currentSlideIndex_].buttonText);
}

void InitialPage::CommunityBanner::goToSlide(int index)
{
    if (index >= 0 && index < (int)slides_.size())
        startTransition(index);
}

void InitialPage::CommunityBanner::nextSlide()
{
    if (slides_.size() <= 1) return;
    int next = (currentSlideIndex_ + 1) % (int)slides_.size();
    startTransition(next);
}

void InitialPage::CommunityBanner::previousSlide()
{
    if (slides_.size() <= 1) return;
    int prev = (currentSlideIndex_ - 1 + (int)slides_.size()) % (int)slides_.size();
    startTransition(prev);
}

void InitialPage::CommunityBanner::addSlide(const Slide& slide)
{
    slides_.push_back(slide);
    repaint();
}

void InitialPage::CommunityBanner::clearSlides()
{
    slides_.clear();
    currentSlideIndex_ = 0;
    previousSlideIndex_ = -1;
    repaint();
}

void InitialPage::CommunityBanner::setAutoScrollInterval(int milliseconds)
{
    autoScrollMs_ = juce::jmax(1000, milliseconds);  // Minimum 1 second
}

void InitialPage::CommunityBanner::setTransitionDuration(int milliseconds)
{
    transitionMs_ = juce::jmax(100, juce::jmin(milliseconds, 1000));  // 100ms - 1000ms
}

void InitialPage::CommunityBanner::setPauseOnHover(bool pause)
{
    pauseOnHover_ = pause;
}

void InitialPage::CommunityBanner::setShowNavigationDots(bool show)
{
    showDots_ = show;
    repaint();
}

void InitialPage::CommunityBanner::loadDefaultSlides()
{
    // Default slides (text-only, no images)
    slides_.push_back({
        {},  // No image
        "COMMUNITY HUB",
        "Connect  -  Share  -  Learn",
        "",  // Default URL
        "OPEN",
        juce::Colour(0xFFFFD700),
        juce::Colours::grey
    });

    slides_.push_back({
        {},
        "Join the Discussion",
        "Share tips, ask questions, get answers",
        "",
        "OPEN",
        juce::Colour(0xFF00FFFF),
        juce::Colours::grey
    });

    slides_.push_back({
        {},
        "Support Development",
        "Help us build new features and sample packs",
        "",
        "DONATE",
        juce::Colour(0xFFFF6B6B),
        juce::Colours::grey
    });
}

void InitialPage::CommunityBanner::loadSlidesFromDirectory(const juce::File& slidesDir)
{
    // Load slides from community_slides directory
    // Expected structure:
    // community_slides/
    //   slide_01.json  (metadata)
    //   slide_01.png   (image)
    //   slide_02.json
    //   slide_02.png
    //   ...

    if (!slidesDir.isDirectory())
    {
        juce::Logger::writeToLog("[CommunityBanner] Slides directory not found: " + slidesDir.getFullPathName());
        return;
    }

    slides_.clear();

    auto jsonFiles = slidesDir.findChildFiles(juce::File::findFiles, false, "*.json");
    jsonFiles.sort();

    for (const auto& jsonFile : jsonFiles)
    {
        loadSlidesFromJson(jsonFile);
    }

    if (slides_.empty())
    {
        loadDefaultSlides();
    }
}

void InitialPage::CommunityBanner::loadSlidesFromJson(const juce::File& jsonFile)
{
    // Parse JSON slide configuration
    auto jsonText = jsonFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonText);

    if (!json.isObject())
    {
        juce::Logger::writeToLog("[CommunityBanner] Invalid JSON in: " + jsonFile.getFileName());
        return;
    }

    Slide slide;
    slide.title = json.getProperty("title", "").toString();
    slide.subtitle = json.getProperty("subtitle", "").toString();
    slide.url = json.getProperty("url", "").toString();
    slide.buttonText = json.getProperty("buttonText", "OPEN").toString();

    // Load image if specified
    auto imageName = json.getProperty("image", "").toString();
    if (imageName.isNotEmpty())
    {
        auto imageFile = jsonFile.getSiblingFile(imageName);
        if (imageFile.existsAsFile())
        {
            slide.image = juce::ImageFileFormat::loadFrom(imageFile);
        }
    }

    // Parse colors if specified
    auto titleColorStr = json.getProperty("titleColor", "").toString();
    if (titleColorStr.isNotEmpty())
        slide.titleColor = juce::Colour::fromString(titleColorStr);

    auto subtitleColorStr = json.getProperty("subtitleColor", "").toString();
    if (subtitleColorStr.isNotEmpty())
        slide.subtitleColor = juce::Colour::fromString(subtitleColorStr);

    slides_.push_back(slide);
}
```

#### 4.7.6 Slide JSON Configuration Format

Each slide can be configured via a JSON file:

```json
{
    "title": "New Sample Packs Available!",
    "subtitle": "Check out our latest drum kits and loops",
    "image": "slide_01.png",
    "url": "https://sample-monsta-community.onrender.com/products",
    "buttonText": "BROWSE",
    "titleColor": "ffFFD700",
    "subtitleColor": "ff888888"
}
```

#### 4.7.7 Directory Structure

```
Sample_monsta5/
├── plugins/
│   └── community_slides/          # Slide assets directory
│       ├── slide_01.json          # Slide 1 configuration
│       ├── slide_01.png           # Slide 1 background image (optional)
│       ├── slide_02.json          # Slide 2 configuration
│       ├── slide_02.png           # Slide 2 background image (optional)
│       ├── slide_03.json          # Slide 3 configuration
│       └── ...
```

#### 4.7.8 Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `autoScrollMs` | 5000 | Time between automatic slide changes (milliseconds) |
| `transitionMs` | 300 | Duration of fade transition (milliseconds) |
| `pauseOnHover` | true | Pause auto-scroll when mouse hovers over banner |
| `showDots` | true | Show navigation dots at bottom of banner |

#### 4.7.9 Integration with InitialPage

```cpp
// In InitialPage constructor:
addAndMakeVisible(communityBanner_);

// Load slides from plugins directory
auto slidesDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
    .getParentDirectory().getChildFile("plugins/community_slides");
communityBanner_.loadSlidesFromDirectory(slidesDir);

// Set auto-scroll interval (5 seconds)
communityBanner_.setAutoScrollInterval(5000);

// Wire up callbacks
communityBanner_.onClick = [this]{ if (onGoCommunity) onGoCommunity(); };
communityBanner_.onSlideClick = [this](const juce::String& url){
    // Could open specific URL or navigate to Community page
    if (onGoCommunity) onGoCommunity();
};
```

#### 4.7.10 Behavior Summary

1. **Auto-Scroll**: Slides automatically advance every 5 seconds (configurable)
2. **Pause on Hover**: Auto-scroll pauses when mouse hovers over banner
3. **Smooth Transitions**: Fade animation (300ms default) between slides
4. **Navigation Dots**: Clickable dots at bottom to jump to specific slide
5. **Click Action**: Clicking banner opens Community Hub (or slide-specific URL)
6. **Dynamic Content**: Slides loaded from JSON files for easy updates
7. **Fallback**: Default text-only slides if no images/JSON found

---

## 5. Database Schema

### 5.1 New Tables (PostgreSQL)

```sql
-- ============================================
-- COMMUNITY HUB DATABASE SCHEMA
-- Run this migration on the existing license server database
-- ============================================

-- Forum Categories
CREATE TABLE IF NOT EXISTS forum_categories (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    description TEXT,
    slug VARCHAR(100) UNIQUE NOT NULL,
    icon VARCHAR(50),
    sort_order INTEGER DEFAULT 0,
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Forum Posts
CREATE TABLE IF NOT EXISTS forum_posts (
    id SERIAL PRIMARY KEY,
    category_id INTEGER NOT NULL REFERENCES forum_categories(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title VARCHAR(255) NOT NULL,
    content TEXT NOT NULL,
    is_pinned BOOLEAN DEFAULT false,
    is_locked BOOLEAN DEFAULT false,
    view_count INTEGER DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Forum Replies
CREATE TABLE IF NOT EXISTS forum_replies (
    id SERIAL PRIMARY KEY,
    post_id INTEGER NOT NULL REFERENCES forum_posts(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    content TEXT NOT NULL,
    is_solution BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- FAQ Questions
CREATE TABLE IF NOT EXISTS faq_questions (
    id SERIAL PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title VARCHAR(255) NOT NULL,
    content TEXT NOT NULL,
    tags VARCHAR(255),
    view_count INTEGER DEFAULT 0,
    is_answered BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- FAQ Answers
CREATE TABLE IF NOT EXISTS faq_answers (
    id SERIAL PRIMARY KEY,
    question_id INTEGER NOT NULL REFERENCES faq_questions(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    content TEXT NOT NULL,
    is_accepted BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- FAQ Votes (for questions)
CREATE TABLE IF NOT EXISTS faq_votes (
    id SERIAL PRIMARY KEY,
    question_id INTEGER NOT NULL REFERENCES faq_questions(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    vote_value INTEGER NOT NULL CHECK (vote_value IN (-1, 1)),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(question_id, user_id)
);

-- Donations
CREATE TABLE IF NOT EXISTS donations (
    id SERIAL PRIMARY KEY,
    user_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    amount_cents INTEGER NOT NULL,
    currency VARCHAR(3) DEFAULT 'USD',
    stripe_session_id VARCHAR(255),
    stripe_payment_intent VARCHAR(255),
    status VARCHAR(20) DEFAULT 'pending',
    message TEXT,
    is_anonymous BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Products (for showcase)
CREATE TABLE IF NOT EXISTS products (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    tagline VARCHAR(255),
    description TEXT,
    price DECIMAL(10, 2) DEFAULT 0,
    currency VARCHAR(3) DEFAULT 'USD',
    image_url VARCHAR(500),
    purchase_url VARCHAR(500),
    features JSONB,
    sort_order INTEGER DEFAULT 0,
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ============================================
-- INDEXES FOR PERFORMANCE
-- ============================================

CREATE INDEX idx_forum_posts_category ON forum_posts(category_id);
CREATE INDEX idx_forum_posts_user ON forum_posts(user_id);
CREATE INDEX idx_forum_posts_created ON forum_posts(created_at DESC);

CREATE INDEX idx_forum_replies_post ON forum_replies(post_id);
CREATE INDEX idx_forum_replies_user ON forum_replies(user_id);

CREATE INDEX idx_faq_questions_user ON faq_questions(user_id);
CREATE INDEX idx_faq_questions_created ON faq_questions(created_at DESC);

CREATE INDEX idx_faq_answers_question ON faq_answers(question_id);
CREATE INDEX idx_faq_votes_question ON faq_votes(question_id);

CREATE INDEX idx_donations_user ON donations(user_id);
CREATE INDEX idx_donations_created ON donations(created_at DESC);

-- ============================================
-- SEED DATA - INITIAL CATEGORIES
-- ============================================

INSERT INTO forum_categories (name, description, slug, icon, sort_order) VALUES
('General Discussion', 'General chat about SAMPLAR and music production', 'general', '💬', 1),
('Feature Requests', 'Suggest new features and vote on ideas', 'features', '💡', 2),
('Bug Reports', 'Report bugs and issues', 'bugs', '🐛', 3),
('Tips & Tutorials', 'Share your tips and learn from others', 'tutorials', '📚', 4),
('Show & Tell', 'Share your creations made with SAMPLAR', 'showcase', '🎵', 5)
ON CONFLICT (slug) DO NOTHING;

-- ============================================
-- SEED DATA - INITIAL PRODUCTS
-- ============================================

INSERT INTO products (name, tagline, description, price, image_url, purchase_url, features, sort_order) VALUES
(
    'Varia',
    'Advanced Sound Variation Engine',
    'Create entirely new sounds by combining and morphing samples with cutting-edge algorithms. Varia takes sample variation to the next level with two unique sound-combining engines.',
    49.99,
    '/static/images/products/varia.png',
    'https://yoursite.com/varia',
    '["Unique sound-combining engines", "Advanced morphing algorithms", "Professional workflow", "Seamless DAW integration"]'::jsonb,
    1
),
(
    'SAMPLAR',
    'The Ultimate Drum Sample Tool',
    'Process, vary, and organize your drum samples with ease. SAMPLAR is free and open source, perfect for producers of all levels.',
    0,
    '/static/images/products/SAMPLAR.png',
    'https://yoursite.com/SAMPLAR',
    '["8 variation engines", "Step sequencer with pattern generation", "Demucs audio separation", "Community-driven development", "FREE & Open Source"]'::jsonb,
    2
)
ON CONFLICT DO NOTHING;
```

### 5.2 Migration Script

```sql
-- migration_001_community_hub.sql
-- Run this to add community hub tables to existing database

BEGIN;

-- Check if migration already applied
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.tables WHERE table_name = 'forum_categories') THEN
        -- Run the full schema creation (copy from above)
        RAISE NOTICE 'Creating community hub tables...';
    ELSE
        RAISE NOTICE 'Community hub tables already exist';
    END IF;
END $$;

COMMIT;
```

---

## 6. API Specification

### 6.1 API Endpoints Summary

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| **Forum** |
| GET | `/api/community/forum/categories` | No | List categories |
| GET | `/api/community/forum/posts/{category_id}` | No | List posts |
| GET | `/api/community/forum/thread/{post_id}` | No | Get thread |
| POST | `/api/community/forum/posts` | Yes | Create post |
| POST | `/api/community/forum/replies` | Yes | Create reply |
| **FAQ** |
| GET | `/api/community/faq/questions` | No | List questions |
| GET | `/api/community/faq/question/{id}` | No | Get question |
| POST | `/api/community/faq/questions` | Yes | Ask question |
| POST | `/api/community/faq/answers` | Yes | Post answer |
| POST | `/api/community/faq/vote` | Yes | Vote on question |
| **Donations** |
| POST | `/api/community/donate/create-checkout` | Yes | Create Stripe session |
| POST | `/api/community/donate/webhook` | No* | Stripe webhook |
| **Products** |
| GET | `/api/community/products` | No | List products |
| **Pages** |
| GET | `/community` | No | Community home |
| GET | `/community/forum` | No | Forum index |
| GET | `/community/faq` | No | FAQ index |
| GET | `/community/donate` | No | Donation page |
| GET | `/community/products` | No | Product showcase |

*Webhook uses Stripe signature verification

### 6.2 Request/Response Examples

#### Create Forum Post
```http
POST /api/community/forum/posts?token={AUTH_TOKEN}
Content-Type: application/json

{
    "category_id": 1,
    "title": "How do I use the step sequencer?",
    "content": "I'm new to SAMPLAR and trying to figure out the step sequencer..."
}
```

Response:
```json
{
    "success": true,
    "post_id": 42
}
```

#### Vote on FAQ Question
```http
POST /api/community/faq/vote?token={AUTH_TOKEN}
Content-Type: application/json

{
    "question_id": 15,
    "vote_value": 1
}
```

Response:
```json
{
    "success": true,
    "action": "created"
}
```

#### Create Donation Checkout
```http
POST /api/community/donate/create-checkout?token={AUTH_TOKEN}
Content-Type: application/json

{
    "amount": 1000
}
```

Response:
```json
{
    "success": true,
    "checkout_url": "https://checkout.stripe.com/pay/cs_xxx",
    "session_id": "cs_xxx"
}
```

---

## 7. Authentication Flow

### 7.1 Token-Based Authentication

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        AUTHENTICATION FLOW                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  SAMPLAR APP                                SERVER                              │
│  ───────                                ──────                              │
│                                                                             │
│  1. User has valid license              2. License validated previously     │
│     (already authenticated)                (session token exists)           │
│           │                                      │                          │
│           ▼                                      │                          │
│  3. User clicks "Community"                      │                          │
│           │                                      │                          │
│           ▼                                      │                          │
│  4. SAMPLAR gets session token                       │                          │
│     from LicenseSystemService                    │                          │
│           │                                      │                          │
│           ▼                                      │                          │
│  5. SAMPLAR loads WebBrowserComponent                │                          │
│     URL: /community?token=xxx ──────────────────►│                          │
│                                                  │                          │
│                                         6. Server validates token           │
│                                            via security_manager             │
│                                                  │                          │
│                                         7. Token valid?                     │
│                                            ├── YES: Return page with user   │
│                                            └── NO: Return page as guest     │
│                                                  │                          │
│  8. WebView displays community ◄─────────────────┘                          │
│     (user pre-authenticated)                                                │
│                                                                             │
│  9. All subsequent requests                                                 │
│     include token in URL/cookie                                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Token Validation (Server-Side)

```python
async def validate_token(token: str) -> Optional[Dict[str, Any]]:
    """
    Validate session token and return user info.
    Reuses existing security_manager logic.
    """
    if not token:
        return None

    # Option 1: Direct database lookup
    db = get_database_service()
    result = await db.execute_query(
        """
        SELECT u.id, u.email, u.created_at
        FROM users u
        JOIN sessions s ON s.user_id = u.id
        WHERE s.token = %s AND s.expires_at > NOW()
        """,
        (token,)
    )

    return result[0] if result else None
```

---

## 8. UI/UX Design

### 8.1 Base Template (base.html)
```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{% block title %}SAMPLAR Community{% endblock %}</title>
    <link rel="stylesheet" href="/static/css/community.css">
</head>
<body>
    <nav class="community-nav">
        <div class="nav-brand">
            <img src="/static/images/SAMPLAR-logo.png" alt="SAMPLAR" class="logo">
            <span>Community</span>
        </div>
        <div class="nav-links">
            <a href="/community" class="{% if page == 'home' %}active{% endif %}">Home</a>
            <a href="/community/forum" class="{% if page == 'forum' %}active{% endif %}">Forum</a>
            <a href="/community/faq" class="{% if page == 'faq' %}active{% endif %}">FAQ</a>
            <a href="/community/donate" class="{% if page == 'donate' %}active{% endif %}">Donate</a>
            <a href="/community/products" class="{% if page == 'products' %}active{% endif %}">Products</a>
        </div>
        <div class="nav-user">
            {% if user %}
                <span class="user-email">{{ user.email }}</span>
            {% else %}
                <span class="guest">Guest</span>
            {% endif %}
        </div>
    </nav>

    <main class="community-content">
        {% block content %}{% endblock %}
    </main>

    <footer class="community-footer">
        <p>&copy; 2025 SAMPLAR. Built with ❤️ by the community.</p>
    </footer>

    <script src="/static/js/community.js"></script>
</body>
</html>
```

### 8.2 Community Home (community_home.html)
```html
{% extends "base.html" %}

{% block title %}SAMPLAR Community{% endblock %}

{% block content %}
<div class="home-hero">
    <h1>Welcome to the SAMPLAR Community</h1>
    <p>Connect, learn, and share with fellow producers</p>
</div>

<div class="home-cards">
    <a href="/community/forum" class="card">
        <div class="card-icon">💬</div>
        <h3>Forum</h3>
        <p>Join discussions, share tips, and get help</p>
    </a>

    <a href="/community/faq" class="card">
        <div class="card-icon">❓</div>
        <h3>FAQ</h3>
        <p>Find answers to common questions</p>
    </a>

    <a href="/community/donate" class="card">
        <div class="card-icon">❤️</div>
        <h3>Support</h3>
        <p>Help fund development with a donation</p>
    </a>

    <a href="/community/products" class="card">
        <div class="card-icon">🎵</div>
        <h3>Products</h3>
        <p>Explore our other tools</p>
    </a>
</div>

<div class="home-recent">
    <h2>Recent Discussions</h2>
    <div id="recent-posts">
        <!-- Loaded via AJAX -->
        <p class="loading">Loading...</p>
    </div>
</div>

<script>
    // Load recent posts
    fetch('/api/community/forum/posts/1?limit=5')
        .then(r => r.json())
        .then(posts => {
            const container = document.getElementById('recent-posts');
            if (posts.length === 0) {
                container.innerHTML = '<p>No posts yet. Be the first!</p>';
                return;
            }
            container.innerHTML = posts.map(p => `
                <div class="post-preview">
                    <a href="/community/forum/thread/${p.id}">${p.title}</a>
                    <span class="meta">${p.reply_count} replies</span>
                </div>
            `).join('');
        });
</script>
{% endblock %}
```

### 8.3 CSS Styles (community.css)
```css
/* ============================================
   SAMPLAR COMMUNITY - STYLES
   Dark theme matching SAMPLAR application
   ============================================ */

:root {
    --bg-primary: #1a1a1a;
    --bg-secondary: #252525;
    --bg-tertiary: #2a2a2a;
    --accent-cyan: #00FFFF;
    --accent-gold: #FFD700;
    --text-primary: #ffffff;
    --text-secondary: #aaaaaa;
    --text-muted: #666666;
    --border-color: #333333;
    --success: #00ff88;
    --error: #ff4444;
}

* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
    background-color: var(--bg-primary);
    color: var(--text-primary);
    line-height: 1.6;
}

/* Navigation */
.community-nav {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 24px;
    background-color: var(--bg-secondary);
    border-bottom: 1px solid var(--border-color);
}

.nav-brand {
    display: flex;
    align-items: center;
    gap: 8px;
    font-weight: bold;
    color: var(--accent-cyan);
}

.nav-brand .logo {
    height: 28px;
}

.nav-links {
    display: flex;
    gap: 24px;
}

.nav-links a {
    color: var(--text-secondary);
    text-decoration: none;
    padding: 8px 12px;
    border-radius: 4px;
    transition: all 0.2s;
}

.nav-links a:hover,
.nav-links a.active {
    color: var(--accent-cyan);
    background-color: var(--bg-tertiary);
}

.nav-user {
    color: var(--text-muted);
}

/* Main Content */
.community-content {
    max-width: 1200px;
    margin: 0 auto;
    padding: 24px;
    min-height: calc(100vh - 120px);
}

/* Home Page */
.home-hero {
    text-align: center;
    padding: 48px 24px;
    background: linear-gradient(135deg, var(--bg-secondary), var(--bg-tertiary));
    border-radius: 12px;
    margin-bottom: 32px;
}

.home-hero h1 {
    color: var(--accent-cyan);
    margin-bottom: 12px;
}

.home-cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 24px;
    margin-bottom: 32px;
}

.card {
    background-color: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 24px;
    text-decoration: none;
    color: var(--text-primary);
    transition: all 0.2s;
}

.card:hover {
    border-color: var(--accent-cyan);
    transform: translateY(-2px);
}

.card-icon {
    font-size: 32px;
    margin-bottom: 12px;
}

.card h3 {
    color: var(--accent-cyan);
    margin-bottom: 8px;
}

.card p {
    color: var(--text-secondary);
    font-size: 14px;
}

/* Forum Styles */
.forum-category {
    background-color: var(--bg-secondary);
    border-radius: 8px;
    margin-bottom: 16px;
    overflow: hidden;
}

.forum-category-header {
    padding: 16px 20px;
    background-color: var(--bg-tertiary);
    border-bottom: 1px solid var(--border-color);
}

.post-list {
    list-style: none;
}

.post-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 20px;
    border-bottom: 1px solid var(--border-color);
}

.post-item:last-child {
    border-bottom: none;
}

.post-item a {
    color: var(--text-primary);
    text-decoration: none;
}

.post-item a:hover {
    color: var(--accent-cyan);
}

.post-meta {
    color: var(--text-muted);
    font-size: 12px;
}

/* Buttons */
.btn {
    display: inline-block;
    padding: 10px 20px;
    border: none;
    border-radius: 6px;
    cursor: pointer;
    font-size: 14px;
    font-weight: 500;
    text-decoration: none;
    transition: all 0.2s;
}

.btn-primary {
    background-color: var(--accent-cyan);
    color: var(--bg-primary);
}

.btn-primary:hover {
    background-color: #00cccc;
}

.btn-secondary {
    background-color: var(--bg-tertiary);
    color: var(--text-primary);
    border: 1px solid var(--border-color);
}

.btn-secondary:hover {
    border-color: var(--accent-cyan);
}

/* Forms */
.form-group {
    margin-bottom: 16px;
}

.form-label {
    display: block;
    margin-bottom: 6px;
    color: var(--text-secondary);
    font-size: 14px;
}

.form-input,
.form-textarea {
    width: 100%;
    padding: 10px 12px;
    background-color: var(--bg-tertiary);
    border: 1px solid var(--border-color);
    border-radius: 6px;
    color: var(--text-primary);
    font-size: 14px;
}

.form-input:focus,
.form-textarea:focus {
    outline: none;
    border-color: var(--accent-cyan);
}

.form-textarea {
    min-height: 150px;
    resize: vertical;
}

/* Donation Page */
.donate-amounts {
    display: flex;
    gap: 12px;
    flex-wrap: wrap;
    margin-bottom: 24px;
}

.amount-btn {
    padding: 16px 32px;
    background-color: var(--bg-secondary);
    border: 2px solid var(--border-color);
    border-radius: 8px;
    color: var(--text-primary);
    font-size: 18px;
    cursor: pointer;
    transition: all 0.2s;
}

.amount-btn:hover,
.amount-btn.selected {
    border-color: var(--accent-gold);
    color: var(--accent-gold);
}

/* Footer */
.community-footer {
    text-align: center;
    padding: 24px;
    color: var(--text-muted);
    font-size: 14px;
    border-top: 1px solid var(--border-color);
}

/* Responsive */
@media (max-width: 768px) {
    .community-nav {
        flex-direction: column;
        gap: 12px;
    }

    .nav-links {
        flex-wrap: wrap;
        justify-content: center;
    }

    .home-cards {
        grid-template-columns: 1fr;
    }
}
```

---

## 9. Stripe Integration

### 9.1 Environment Variables

```bash
# Add to Render.com environment variables
STRIPE_SECRET_KEY=sk_live_xxx
STRIPE_PUBLIC_KEY=pk_live_xxx
STRIPE_WEBHOOK_SECRET=whsec_xxx
```

### 9.2 Webhook Setup

1. Go to Stripe Dashboard → Webhooks
2. Add endpoint: `https://your-server.render.com/api/community/donate/webhook`
3. Select events: `checkout.session.completed`
4. Copy webhook secret to environment variable

### 9.3 Donation Page JavaScript

```javascript
// donation.js

const STRIPE_PUBLIC_KEY = '{{ stripe_public_key }}';  // Injected by server

let selectedAmount = 500;  // Default $5.00

document.querySelectorAll('.amount-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.amount-btn').forEach(b => b.classList.remove('selected'));
        btn.classList.add('selected');
        selectedAmount = parseInt(btn.dataset.amount);
    });
});

document.getElementById('donate-btn').addEventListener('click', async () => {
    const btn = document.getElementById('donate-btn');
    btn.disabled = true;
    btn.textContent = 'Processing...';

    try {
        const response = await fetch('/api/community/donate/create-checkout', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ amount: selectedAmount })
        });

        const data = await response.json();

        if (data.checkout_url) {
            window.location.href = data.checkout_url;
        } else {
            alert('Error: ' + (data.error || 'Unknown error'));
            btn.disabled = false;
            btn.textContent = 'Donate';
        }
    } catch (error) {
        alert('Network error. Please try again.');
        btn.disabled = false;
        btn.textContent = 'Donate';
    }
});
```

---

## 10. Deployment Guide

### 10.1 Server-Side Deployment (Render.com)

1. **Add Plugin Files**
   ```bash
   # In your license_server repository
   mkdir -p plugins/community_hub/templates
   mkdir -p plugins/community_hub/static
   # Copy all plugin files
   ```

2. **Update requirements.txt**
   ```
   # Add if not already present
   stripe>=5.0.0
   jinja2>=3.0.0
   ```

3. **Add Environment Variables in Render Dashboard**
   ```
   STRIPE_SECRET_KEY=sk_live_xxx
   STRIPE_PUBLIC_KEY=pk_live_xxx
   STRIPE_WEBHOOK_SECRET=whsec_xxx
   ```

4. **Run Database Migration**
   ```bash
   # Connect to PostgreSQL and run migration
   psql $DATABASE_URL < migration_001_community_hub.sql
   ```

5. **Deploy**
   ```bash
   git add .
   git commit -m "Add community hub plugin"
   git push origin main
   # Render auto-deploys
   ```

### 10.2 Client-Side Build (SAMPLAR)

1. **Add Source Files**
   ```
   src/ui/Pages/CommunityPage.h
   src/ui/Pages/CommunityPage.cpp
   ```

2. **Update CMakeLists.txt**
   ```cmake
   target_sources(SM5App PRIVATE
       src/ui/Pages/CommunityPage.h
       src/ui/Pages/CommunityPage.cpp
   )
   ```

3. **Configure Server URL**
   ```cpp
   // In MainWindow.cpp or config
   communityPage_->setServerUrl("https://your-license-server.render.com");
   ```

4. **Build**
   ```bash
   cmake --build build --config Release
   ```

---

## 11. Testing Strategy

### 11.1 Server-Side Tests

```python
# tests/test_community.py

import pytest
from fastapi.testclient import TestClient

def test_forum_categories(client):
    """Test getting forum categories"""
    response = client.get("/api/community/forum/categories")
    assert response.status_code == 200
    data = response.json()
    assert isinstance(data, list)

def test_create_post_requires_auth(client):
    """Test that creating post requires authentication"""
    response = client.post("/api/community/forum/posts", json={
        "category_id": 1,
        "title": "Test",
        "content": "Test content"
    })
    assert response.status_code == 200
    data = response.json()
    assert "error" in data

def test_donate_checkout(client, auth_token):
    """Test donation checkout creation"""
    response = client.post(
        f"/api/community/donate/create-checkout?token={auth_token}",
        json={"amount": 1000}
    )
    assert response.status_code == 200
    data = response.json()
    assert "checkout_url" in data or "error" in data
```

### 11.2 Client-Side Testing

1. **Manual Testing Checklist**
   - [ ] Community page loads
   - [ ] User is pre-authenticated
   - [ ] Forum posts display
   - [ ] Can create new post
   - [ ] FAQ questions display
   - [ ] Can vote on questions
   - [ ] Donation flow works
   - [ ] Products display

2. **WebView Testing**
   - [ ] Page loads in embedded browser
   - [ ] Navigation works
   - [ ] Forms submit correctly
   - [ ] Refresh button works

---

## 12. Security Considerations

### 12.1 Authentication Security

- All write operations require valid session token
- Tokens validated server-side for every request
- Tokens expire after inactivity period
- HTTPS required for all communication

### 12.2 Input Validation

```python
# All user input sanitized
from html import escape

def sanitize_content(content: str) -> str:
    """Sanitize user-generated content"""
    # Remove script tags
    content = re.sub(r'<script[^>]*>.*?</script>', '', content, flags=re.DOTALL)
    # Escape HTML
    content = escape(content)
    # Allow basic formatting (optional)
    content = content.replace('&lt;b&gt;', '<b>').replace('&lt;/b&gt;', '</b>')
    return content
```

### 12.3 Rate Limiting

```python
# Reuse existing security_manager rate limiting
# Apply to all community endpoints
RATE_LIMITS = {
    "forum.create_post": "10/minute",
    "forum.create_reply": "30/minute",
    "faq.vote": "60/minute",
    "donate.checkout": "5/minute"
}
```

### 12.4 Stripe Security

- Webhook signature verification required
- No sensitive payment data stored
- All transactions logged for audit

---

## Appendix A: File Checklist

### Server-Side Files to Create

```
plugins/community_hub/
├── __init__.py                  ✓ Documented
├── community_service.py         ✓ Documented
├── forum_service.py             ✓ Documented
├── faq_service.py               ✓ Documented
├── donation_service.py          ✓ Documented
├── product_service.py           ✓ Documented
├── templates/
│   ├── base.html                ✓ Documented
│   ├── community_home.html      ✓ Documented
│   ├── forum/
│   │   ├── index.html           To implement
│   │   ├── category.html        To implement
│   │   ├── thread.html          To implement
│   │   └── new_post.html        To implement
│   ├── faq/
│   │   ├── index.html           To implement
│   │   ├── question.html        To implement
│   │   └── ask.html             To implement
│   ├── donate/
│   │   ├── index.html           To implement
│   │   └── success.html         To implement
│   └── products/
│       └── index.html           To implement
└── static/
    ├── css/
    │   └── community.css        ✓ Documented
    └── js/
        └── community.js         To implement
```

### Client-Side Files to Create

```
src/ui/Pages/
├── CommunityPage.h              ✓ Documented
└── CommunityPage.cpp            ✓ Documented
```

### Database

```
migration_001_community_hub.sql  ✓ Documented
```

---

## Appendix B: Quick Reference

### Server API Quick Reference

| Action | Method | Endpoint |
|--------|--------|----------|
| List forum categories | GET | `/api/community/forum/categories` |
| List posts in category | GET | `/api/community/forum/posts/{category_id}` |
| Create post | POST | `/api/community/forum/posts` |
| Create reply | POST | `/api/community/forum/replies` |
| List FAQ questions | GET | `/api/community/faq/questions` |
| Create question | POST | `/api/community/faq/questions` |
| Vote on question | POST | `/api/community/faq/vote` |
| Create donation | POST | `/api/community/donate/create-checkout` |
| List products | GET | `/api/community/products` |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `STRIPE_SECRET_KEY` | Stripe API secret key |
| `STRIPE_PUBLIC_KEY` | Stripe publishable key |
| `STRIPE_WEBHOOK_SECRET` | Stripe webhook signing secret |

---

**Document Version:** 1.0
**Last Updated:** December 15, 2025
**Status:** Ready for Implementation
