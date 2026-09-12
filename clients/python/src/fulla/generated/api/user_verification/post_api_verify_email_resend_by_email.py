from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.post_api_verify_email_resend_by_email_body import PostApiVerifyEmailResendByEmailBody
from ...types import UNSET, Response, Unset


def _get_kwargs(
    *,
    body: PostApiVerifyEmailResendByEmailBody | Unset = UNSET,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/api/verify-email/resend-by-email",
    }

    if not isinstance(body, Unset):
        _kwargs["json"] = body.to_dict()

    headers["Content-Type"] = "application/json"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Any | None:
    if response.status_code == 200:
        return None

    if response.status_code == 400:
        return None

    if response.status_code == 429:
        return None

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Response[Any]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: PostApiVerifyEmailResendByEmailBody | Unset = UNSET,
) -> Response[Any]:
    """Resend Verification Email (by email address)

     Resend the email verification link, identifying the account by email address. Unauthenticated (see
    issue 198): a self-registered user holds no token yet. Rate-limited per (ip, email) with an
    identical response for unknown, already-verified, and emailed addresses (anti-enumeration).

    Args:
        body (PostApiVerifyEmailResendByEmailBody | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: PostApiVerifyEmailResendByEmailBody | Unset = UNSET,
) -> Response[Any]:
    """Resend Verification Email (by email address)

     Resend the email verification link, identifying the account by email address. Unauthenticated (see
    issue 198): a self-registered user holds no token yet. Rate-limited per (ip, email) with an
    identical response for unknown, already-verified, and emailed addresses (anti-enumeration).

    Args:
        body (PostApiVerifyEmailResendByEmailBody | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)
