<script setup lang="ts">
import { onMounted, computed, ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useAuthStore } from '../../stores/auth'
import { authService } from '../../services/authService'
import { normalizeError, type NormalizedError } from '../../services/errorAdapter'
import { getErrorMessage } from '../../services/messages'

const { t } = useI18n()
const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
// #158: catalog-backed errors are stored as NormalizedError and resolved at
// render time (errorText) so switching locale re-translates text on screen;
// plain strings (chrome copy via t()) keep snapshot semantics.
const error = ref<NormalizedError | string | null>(null)
const errorText = computed(() => {
  const e = error.value
  if (!e) return ''
  return typeof e === 'string' ? e : getErrorMessage(e.code)
})
// Secondary detail: the raw error_description from the redirect, kept only
// when it exists and adds something the catalog-resolved message lacks.
const errorDetail = ref('')
// U-4: the code belongs to a flow this SPA did not initiate (external app
// drove the user through authorize with its own PKCE verifier) — show the
// completion notice instead of trying to redeem.
const externalFlow = ref(false)

onMounted(async () => {
  const code = route.query.code as string
  const errorParam = route.query.error as string

  if (typeof errorParam === 'string' && errorParam) {
    // Keep the raw protocol error code — the catalog message resolves at
    // render time so a locale switch re-translates it (#158). Covers every
    // OAuth2/OIDC code; unknown codes fall back to the generic message,
    // instead of rendering error_description raw.
    error.value = {
      code: errorParam,
      message: getErrorMessage(errorParam),
      request_id: '',
      httpStatus: 400,
    }
    const description = route.query.error_description
    errorDetail.value =
      typeof description === 'string' && description && description !== errorText.value
        ? description
        : ''
    return
  }

  if (!code) {
    error.value = t('oauth.callback.noCode')
    return
  }

  // U-4 (browser-e2e 2026-09-08): redeeming a code without OUR PKCE verifier
  // always fails (PKCE is force-enabled server-side) and consumes the
  // one-time code that the flow's initiator still needs. Only redeem when
  // this SPA stashed a verifier for the flow; otherwise this landing is on
  // behalf of an external application.
  if (!authService.hasStashedVerifier()) {
    externalFlow.value = true
    return
  }

  try {
    await auth.exchangeCode(code)
    router.replace('/')
  } catch (e: unknown) {
    error.value = normalizeError(e)
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center">
    <div class="text-center">
      <div
        v-if="errorText"
        class="p-6 bg-error-50 border border-error-200 rounded-lg max-w-md"
      >
        <p class="text-error-700 font-medium">
          {{ $t('oauth.callback.errorTitle') }}
        </p>
        <p class="text-error-600 text-sm mt-2">
          {{ errorText }}
        </p>
        <p
          v-if="errorDetail"
          class="text-error-500 text-xs mt-1.5 break-words"
        >
          {{ errorDetail }}
        </p>
        <router-link
          to="/login"
          class="mt-4 inline-block text-brand-600 hover:text-brand-800"
        >
          {{ $t('common.backToLogin') }}
        </router-link>
      </div>
      <div v-else-if="externalFlow">
        <div class="w-14 h-14 rounded-2xl bg-success-50 flex items-center justify-center mx-auto mb-4">
          <svg
            class="w-7 h-7 text-success-600"
            viewBox="0 0 20 20"
            fill="currentColor"
          >
            <path
              fill-rule="evenodd"
              d="M16.707 5.293a1 1 0 010 1.414l-8 8a1 1 0 01-1.414 0l-4-4a1 1 0 011.414-1.414L8 12.586l7.293-7.293a1 1 0 011.414 0z"
              clip-rule="evenodd"
            />
          </svg>
        </div>
        <p
          class="text-neutral-900 font-medium"
          data-testid="callback-external-flow"
        >
          {{ $t('oauth.callback.externalFlow') }}
        </p>
        <p class="text-neutral-500 text-sm mt-2">
          {{ $t('oauth.callback.externalFlowHint') }}
        </p>
      </div>
      <div v-else>
        <div class="animate-spin w-8 h-8 border-4 border-brand-600 border-t-transparent rounded-full mx-auto" />
        <p class="mt-4 text-neutral-600">
          {{ $t('oauth.callback.completing') }}
        </p>
      </div>
    </div>
  </div>
</template>
