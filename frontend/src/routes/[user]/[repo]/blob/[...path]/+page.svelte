<script lang="ts">
	import { page } from '$app/stores';
	import { onMount, onDestroy } from 'svelte';
	import {
		getBlobUrl,
		getGlbUrl,
		getGlbStatus,
		getRepo,
		formatBytes,
		is3DFile,
		type RepoInfo
	} from '$lib/api';
	import GltfViewer from '$lib/components/GltfViewer.svelte';

	let user = $derived($page.params.user);
	let repo = $derived($page.params.repo);

	// [...path] = "ref/path/to/file"
	let rawPath = $derived($page.params.path ?? '');
	let ref = $derived(rawPath.split('/')[0] ?? 'main');
	let filePath = $derived(rawPath.split('/').slice(1).join('/'));
	let fileName = $derived(filePath.split('/').pop() ?? '');

	let is3d = $derived(is3DFile(fileName));

	let textContent = $state<string | null>(null);
	let glbStatus = $state<'pending' | 'processing' | 'ready' | 'error'>('pending');
	let glbUrl = $state('');
	let loading = $state(true);
	let error = $state('');
	let fileSize = $state(0);
	let commitSha = $state('');

	let pollInterval: ReturnType<typeof setInterval> | null = null;

	// Build line-numbered content
	let lines = $derived(textContent !== null ? textContent.split('\n') : []);

	// Breadcrumb
	let breadcrumbParts = $derived((() => {
		const parts: { label: string; href: string }[] = [
			{ label: repo, href: `/${user}/${repo}` }
		];
		const segments = filePath.split('/').filter(Boolean);
		segments.forEach((seg, i) => {
			const path = segments.slice(0, i + 1).join('/');
			const isLast = i === segments.length - 1;
			const href = isLast
				? `/${user}/${repo}/blob/${ref}/${path}`
				: `/${user}/${repo}/tree/${ref}/${path}`;
			parts.push({ label: seg, href });
		});
		return parts;
	})());

	async function getCommitShaForRef(): Promise<string> {
		// We need the blob sha for glb status — use the ref as sha approximation
		// The API uses sha in GLB endpoints; we'll use ref if it looks like a sha, else fetch commits
		if (/^[0-9a-f]{40}$/i.test(ref)) return ref;
		// For branch names, get latest commit sha
		try {
			const { listCommits } = await import('$lib/api');
			const commits = await listCommits(user, repo, ref, 1);
			return commits[0]?.sha ?? ref;
		} catch {
			return ref;
		}
	}

	async function loadFile() {
		loading = true;
		error = '';
		textContent = null;
		glbUrl = '';

		try {
			const sha = await getCommitShaForRef();
			commitSha = sha;

			const blobUrl = getBlobUrl(user, repo, ref, filePath);

			if (is3d) {
				// Start with status check
				glbUrl = getGlbUrl(user, repo, sha, filePath);
				try {
					const statusResult = await getGlbStatus(user, repo, sha, filePath);
					glbStatus = statusResult.status;
				} catch {
					// GLB not yet queued — status defaults to pending
					glbStatus = 'pending';
				}

				// Poll until ready or error
				if (glbStatus !== 'ready' && glbStatus !== 'error') {
					startPolling(sha);
				}
			} else {
				// Fetch raw text
				const res = await fetch(blobUrl);
				if (!res.ok) throw new Error(`Failed to fetch file: ${res.status}`);
				fileSize = parseInt(res.headers.get('content-length') ?? '0', 10);
				textContent = await res.text();
			}
		} catch (e: any) {
			error = e.message || 'Failed to load file';
		} finally {
			loading = false;
		}
	}

	function startPolling(sha: string) {
		stopPolling();
		pollInterval = setInterval(async () => {
			try {
				const result = await getGlbStatus(user, repo, sha, filePath);
				glbStatus = result.status;
				if (result.status === 'ready' || result.status === 'error') {
					stopPolling();
				}
			} catch {
				// keep polling
			}
		}, 2000);
	}

	function stopPolling() {
		if (pollInterval !== null) {
			clearInterval(pollInterval);
			pollInterval = null;
		}
	}

	onMount(loadFile);
	onDestroy(stopPolling);
</script>

<svelte:head>
	<title>{fileName} - {user}/{repo} - git4cad</title>
</svelte:head>

<div class="blob-page">
	<!-- Breadcrumb -->
	<div class="breadcrumb mb-3">
		{#each breadcrumbParts as part, i (part.href)}
			{#if i > 0}<span class="breadcrumb-sep">/</span>{/if}
			{#if i === breadcrumbParts.length - 1}
				<span class="bc-current">{part.label}</span>
			{:else}
				<a href={part.href} class="bc-link">{part.label}</a>
			{/if}
		{/each}
	</div>

	{#if loading}
		<div class="loading-state">
			<div class="spinner"></div>
			<span>Loading file...</span>
		</div>
	{:else if error}
		<div class="alert alert-error">{error}</div>
	{:else if is3d}
		<!-- 3D viewer -->
		<div class="file-header">
			<div class="file-info">
				<span class="file-name">{fileName}</span>
				<span class="file-badge badge-3d">3D</span>
			</div>
			<div class="file-actions">
				<a
					href={getBlobUrl(user, repo, ref, filePath)}
					class="btn btn-sm"
					download={fileName}
				>
					<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
						<polyline points="7 10 12 15 17 10"/>
						<line x1="12" y1="15" x2="12" y2="3"/>
					</svg>
					Download
				</a>
			</div>
		</div>
		<GltfViewer {glbUrl} status={glbStatus} />
	{:else}
		<!-- Text viewer -->
		<div class="file-header">
			<div class="file-info">
				<span class="file-name">{fileName}</span>
				{#if fileSize > 0}
					<span class="file-size text-muted text-sm">{formatBytes(fileSize)}</span>
				{/if}
				<span class="line-count text-muted text-sm">{lines.length} lines</span>
			</div>
			<div class="file-actions">
				<a
					href={getBlobUrl(user, repo, ref, filePath)}
					class="btn btn-sm"
					download={fileName}
				>
					<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
						<polyline points="7 10 12 15 17 10"/>
						<line x1="12" y1="15" x2="12" y2="3"/>
					</svg>
					Raw
				</a>
			</div>
		</div>

		<div class="code-viewer">
			<table class="code-table" aria-label="File content">
				<tbody>
					{#each lines as line, i (i)}
						<tr>
							<td class="line-num" aria-hidden="true">{i + 1}</td>
							<td class="line-code">{line}</td>
						</tr>
					{/each}
				</tbody>
			</table>
		</div>
	{/if}
</div>

<style>
	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 24px 0;
		color: var(--color-text-muted);
	}

	.bc-link {
		color: var(--color-accent);
		text-decoration: none;
		font-weight: 500;
	}

	.bc-link:hover {
		text-decoration: underline;
	}

	.bc-current {
		color: var(--color-text);
		font-weight: 600;
	}

	.file-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		padding: 10px 16px;
		background-color: var(--color-surface-hover);
		border: 1px solid var(--color-border);
		border-bottom: none;
		border-radius: var(--radius) var(--radius) 0 0;
		gap: 12px;
	}

	.file-info {
		display: flex;
		align-items: center;
		gap: 10px;
		min-width: 0;
		flex: 1;
	}

	.file-name {
		font-size: 14px;
		font-weight: 600;
		color: var(--color-text);
		font-family: var(--font-mono);
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}

	.file-badge {
		font-size: 11px;
		font-weight: 600;
		padding: 2px 6px;
		border-radius: 20px;
		border: 1px solid;
		flex-shrink: 0;
	}

	.badge-3d {
		color: var(--color-accent);
		border-color: rgba(47, 129, 247, 0.4);
		background-color: rgba(47, 129, 247, 0.1);
	}

	.file-size,
	.line-count {
		flex-shrink: 0;
	}

	.file-actions {
		flex-shrink: 0;
	}

	/* Code viewer */
	.code-viewer {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: 0 0 var(--radius) var(--radius);
		overflow: auto;
	}

	.code-table {
		width: 100%;
		border-collapse: collapse;
		font-family: var(--font-mono);
		font-size: 12px;
		line-height: 1.6;
	}

	.code-table tr {
		border-bottom: none;
	}

	.code-table tr:hover {
		background-color: rgba(255, 255, 255, 0.03);
	}

	.code-table td {
		padding: 0;
		border-bottom: none;
		vertical-align: top;
	}

	.line-num {
		user-select: none;
		text-align: right;
		padding: 0 12px;
		min-width: 48px;
		color: var(--color-text-muted);
		background-color: var(--color-surface-hover);
		border-right: 1px solid var(--color-border);
		font-size: 12px;
		cursor: pointer;
		position: sticky;
		left: 0;
	}

	.line-code {
		padding: 0 16px;
		white-space: pre;
		color: var(--color-text);
		width: 100%;
	}

	/* 3D viewer below file header */
	.blob-page :global(.viewer-wrapper) {
		border-top: none;
		border-radius: 0 0 var(--radius) var(--radius);
	}
</style>
