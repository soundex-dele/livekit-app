import {useEffect, useRef, useState} from 'react';
import {Alert, App, Button, Card, Descriptions, Drawer, Empty, Form, Input, Modal, Pagination, Select, Skeleton, Space, Table, Tabs, Typography} from 'antd';
import {useQuery, useQueryClient} from '@tanstack/react-query';
import {useAdminScope, useAVCapabilities} from './api';
import {actionNames, businessName, businessNames, formatTime, memberActions, orgPath, resourceActions, stateNames} from './domain';
import {emptyTable, NoOrganization, PageHeading, RequestError, StateTag} from './components';
import {OperationTable} from './operations';
import type {ActionInput, ActionResult, Business, GovernanceAction, Member, Page, Resource, ResourceDetail} from './types';

interface Choice {action: GovernanceAction; target?: string; name?: string}

function GovernanceDialog({choice, detail, onCancel, onSuccess}: {choice: Choice; detail: ResourceDetail; onCancel: () => void; onSuccess: (result: ActionResult) => void}) {
  const scope = useAdminScope();
  const [form] = Form.useForm<{reason: string}>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<unknown>();
  const key = useRef(crypto.randomUUID());
  const input = useRef<ActionInput>();
  const [submitted, setSubmitted] = useState(false);
  const alive = useRef(true);
  useEffect(() => {alive.current = true; return () => {alive.current = false;};}, []);
  async function submit({reason}: {reason: string}) {
    setBusy(true); setError(undefined);
    input.current ??= {action: choice.action, reason: reason.trim(), target: choice.target, version: detail.resource.version};
    setSubmitted(true);
    try {
      const result = await scope.client.mutate<ActionResult>('POST', `${orgPath(detail.resource.organization_id)}/resources/${encodeURIComponent(detail.resource.id)}/actions`, input.current, {key: key.current});
      if (alive.current) onSuccess(result);
    } catch (cause) {if (alive.current) setError(cause);} finally {if (alive.current) setBusy(false);}
  }
  return <Modal title={actionNames[choice.action]} open onCancel={() => !busy && onCancel()} footer={null} closable={!busy} maskClosable={false} destroyOnClose>
    <Space direction="vertical" size="middle" style={{width: '100%'}}>
      <Alert type={choice.action === 'request-unmute' || choice.action === 'chat-unmute' ? 'info' : 'warning'} showIcon message={detail.resource.title || detail.resource.id} description={<span>组织：{detail.resource.organization_id}{choice.target && <><br/>成员：{choice.name || choice.target}</>}<br/>{choice.action === 'request-unmute' ? '发送开麦邀请，由成员自行决定是否开启麦克风。' : '操作将更新业务状态；涉及媒体权限时，清理会由后台任务继续执行。'}</span>}/>
      {!!error && <RequestError error={error}/>}
      <Form form={form} layout="vertical" onFinish={values => void submit(values)}><Form.Item label="操作原因" name="reason" rules={[{required: true, whitespace: true, message: '请填写操作原因。'}, {max: 500, message: '操作原因最多 500 字。'}]}><Input.TextArea autoFocus rows={3} maxLength={500} showCount placeholder="说明本次操作原因，将保留在治理记录中" disabled={busy || submitted}/></Form.Item>{submitted && !!error && <Typography.Paragraph type="secondary">再次提交将重试同一请求。若需修改原因或刷新状态，请关闭此窗口后重新操作。</Typography.Paragraph>}<div className="av-modal-actions"><Button onClick={onCancel} disabled={busy}>取消</Button><Button htmlType="submit" type="primary" danger={!['request-unmute', 'chat-unmute'].includes(choice.action)} loading={busy}>确认{actionNames[choice.action]}</Button></div></Form>
    </Space>
  </Modal>;
}

function MemberList({detail, select}: {detail: ResourceDetail; select: (choice: Choice) => void}) {
  const controls = (member: Member) => <Space wrap>{memberActions(detail, member).map(action => <Button size="small" key={action} danger={action === 'kick'} onClick={() => select({action, target: member.subject_id, name: member.name})}>{actionNames[action]}</Button>)}</Space>;
  const columns = [{title: '成员', key: 'name', render: (_: unknown, member: Member) => <><Typography.Text>{member.name || member.subject_id}</Typography.Text><div className="av-id av-secondary">{member.subject_id}</div></>}, {title: '角色', dataIndex: 'role'}, {title: '状态', key: 'state', render: (_: unknown, member: Member) => <Space wrap><StateTag state={member.state}/>{member.muted && <span>{detail.resource.business === 'conference' ? '已静音' : '已禁言'}</span>}{member.stage && <StateTag state={member.stage}/>}</Space>}, {title: '治理', key: 'actions', render: (_: unknown, member: Member) => controls(member)}];
  return <><Table className="av-desktop-table" rowKey="subject_id" locale={emptyTable} size="small" dataSource={detail.members} columns={columns} pagination={{pageSize: 10, showSizeChanger: false}} scroll={{x: 650}}/><div className="av-mobile-cards">{detail.members.map(member => <Card key={member.subject_id} size="small" title={member.name || member.subject_id} extra={<StateTag state={member.state}/>}><Typography.Paragraph className="av-id">{member.subject_id}</Typography.Paragraph>{controls(member)}</Card>)}{!detail.members.length && <Empty description="暂无成员"/>}</div></>;
}

function DetailSummary({detail}: {detail: ResourceDetail}) {
  const {resource, details} = detail;
  const items = [{key: 'id', label: '资源编号', children: resource.id}, {key: 'org', label: '组织', children: resource.organization_id}, {key: 'owner', label: '发起人', children: resource.owner}, {key: 'state', label: '业务状态', children: <StateTag state={resource.state}/>}, {key: 'created', label: '创建时间', children: formatTime(resource.created)}, {key: 'updated', label: '更新时间', children: formatTime(resource.updated)}];
  if (details.meeting) items.push(...[{key: 'number', label: '会议号', children: details.meeting.number}, {key: 'host', label: '主持人', children: details.meeting.host}, {key: 'start', label: '开始时间', children: formatTime(details.meeting.starts_at)}, {key: 'waiting', label: '等候室', children: details.meeting.waiting_room ? '已开启' : '未开启'}, {key: 'locked', label: '锁定状态', children: details.meeting.locked ? '已锁定' : '未锁定'}]);
  if (details.cast) items.push({key: 'sender', label: '发送端', children: details.cast.sender || '—'}, {key: 'receiver', label: '接收端', children: details.cast.receiver || '—'});
  if (details.remote) items.push({key: 'host', label: '被控端', children: details.remote.host}, {key: 'controller', label: '控制端', children: details.remote.controller || '—'});
  if (details.live) items.push({key: 'host', label: '主播', children: details.live.host}, {key: 'room', label: '直播间', children: details.live.room_id}, {key: 'pk', label: 'PK 场次', children: details.live.pk_id || '—'});
  return <><Descriptions size="small" column={1} bordered items={items}/>{details.remote && <><Typography.Title level={4}>控制授权</Typography.Title><Alert type="info" showIcon message="管理员可以撤销授权。重新控制仍须被控端再次同意。"/><Table rowKey="id" size="small" locale={emptyTable} dataSource={details.remote.control_grants} pagination={{pageSize: 5, showSizeChanger: false}} columns={[{title: '授权编号', dataIndex: 'id', ellipsis: true}, {title: '状态', dataIndex: 'state', render: (state: string) => <StateTag state={state}/>}, {title: '到期时间', dataIndex: 'expires', render: formatTime}]} scroll={{x: 500}}/></>}</>;
}

function ResourceDrawer({resource, onClose}: {resource: Resource; onClose: () => void}) {
  const scope = useAdminScope();
  const cache = useQueryClient();
  const {message} = App.useApp();
  const [choice, setChoice] = useState<Choice | null>(null);
  const [result, setResult] = useState<ActionResult | null>(null);
  const detail = useQuery({queryKey: [...scope.key, 'detail', resource.organization_id, resource.id], queryFn: ({signal}) => scope.client.get<ResourceDetail>(`${orgPath(resource.organization_id)}/resources/${encodeURIComponent(resource.id)}`, {signal}), refetchInterval: 10000, refetchIntervalInBackground: false});
  const value = detail.data;
  function success(response: ActionResult) {setResult(response); setChoice(null); message.success('业务操作已受理。'); void cache.invalidateQueries({queryKey: scope.key});}
  return <Drawer title={resource.title || `${businessName(resource.business)}详情`} open onClose={onClose} width={920} destroyOnClose extra={<Button onClick={() => void detail.refetch()} loading={detail.isFetching}>刷新详情</Button>}>
    <Space direction="vertical" size="middle" style={{width: '100%'}}>
      {detail.error && <RequestError error={detail.error} retry={() => void detail.refetch()}/>}
      {detail.isLoading && <Skeleton active/>}
      {value && <>
        {!value.resource.module_enabled && <Alert type="info" showIcon message="该业务已关闭，历史记录仍可查询，必要的终止与清理操作仍可执行。"/>}
        {value.members_truncated && <Alert type="info" showIcon message="成员数量较多，当前显示受限数量的成员记录。"/>}
        <Space wrap>{resourceActions(value.resource).map(action => <Button danger key={action} disabled={action === 'revoke-control' && !value.details.remote?.control_grants.some(grant => ['pending', 'active'].includes(grant.state))} onClick={() => setChoice({action})}>{actionNames[action]}</Button>)}</Space>
        {result && <Alert type="success" showIcon message={`业务状态：${stateNames[result.state] || result.state}`} description={result.operation_ids.length ? '媒体操作已排队。请在“媒体操作”页签查看最终结果。' : '操作已记录，无需额外媒体清理。'}/>}
        <Tabs items={[
          {key: 'summary', label: '基本信息', children: <DetailSummary detail={value}/>},
          {key: 'members', label: `成员（${value.members.length}）`, children: <MemberList detail={value} select={setChoice}/>},
          {key: 'bindings', label: '媒体房间', children: <Table locale={emptyTable} rowKey="id" size="small" dataSource={value.bindings} pagination={false} columns={[{title: '节点', dataIndex: 'node_id'}, {title: '用途', dataIndex: 'purpose'}, {title: '状态', dataIndex: 'state', render: (state: string) => <StateTag state={state}/>}, {title: '代次', dataIndex: 'generation'}]}/>},
          {key: 'operations', label: '媒体操作', children: <OperationTable organization={resource.organization_id} resource={resource.id} operationIds={result?.operation_ids}/>},
          {key: 'actions', label: '治理记录', children: <Table locale={emptyTable} rowKey="id" size="small" dataSource={value.actions} pagination={{pageSize: 10, showSizeChanger: false}} columns={[{title: '操作', dataIndex: 'action', render: (action: GovernanceAction) => actionNames[action] || action}, {title: '操作者', dataIndex: 'actor', ellipsis: true}, {title: '原因', dataIndex: 'reason'}, {title: '时间', dataIndex: 'created', render: formatTime}]} scroll={{x: 600}}/>},
        ]}/>
        {choice && <GovernanceDialog key={`${choice.action}:${choice.target || ''}`} choice={choice} detail={value} onCancel={() => setChoice(null)} onSuccess={success}/>}
      </>}
    </Space>
  </Drawer>;
}

export function ResourcesPage({business}: {business: Business}) {
  const scope = useAdminScope();
  const capabilities = useAVCapabilities();
  const [form] = Form.useForm();
  const [filters, setFilters] = useState<Record<string, string>>({});
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(20);
  const [selected, setSelected] = useState<Resource | null>(null);
  useEffect(() => {setSelected(null); setFilters({}); setPage(1); form.resetFields();}, [scope.identity, business, form]);
  const data = useQuery({queryKey: [...scope.key, 'resources', business, filters, page, pageSize], enabled: scope.allowed, queryFn: ({signal}) => scope.client.get<Page<Resource>>(scope.path + '/resources', {signal, query: {business, ...filters, limit: pageSize, offset: (page - 1) * pageSize}}), refetchInterval: 10000, refetchIntervalInBackground: false});
  if (!scope.allowed) return <NoOrganization/>;
  const name = businessNames[business];
  const columns = [{title: `${name}名称`, key: 'name', render: (_: unknown, row: Resource) => <Button type="link" className="av-table-link" onClick={() => setSelected(row)}>{row.title || row.id}</Button>}, {title: '状态', dataIndex: 'state', render: (state: string) => <StateTag state={state}/>}, ...(!scope.organization ? [{title: '组织', dataIndex: 'organization_id', ellipsis: true}] : []), {title: '发起人', dataIndex: 'owner', ellipsis: true}, {title: '创建时间', dataIndex: 'created', render: formatTime}, {title: '操作', key: 'action', render: (_: unknown, row: Resource) => <Button size="small" onClick={() => setSelected(row)}>查看详情</Button>}];
  return <div className="av-page"><PageHeading title={`${name}管理`} description={`查看${name}记录、成员状态和治理进度。所有管理操作均记录原因与操作者。`} extra={<Button onClick={() => void data.refetch()} loading={data.isFetching}>刷新列表</Button>}/>
    {capabilities.data && !capabilities.data.modules.includes(business) && <Alert type="info" showIcon message="业务模块已关闭，当前展示历史记录。"/>}
    <Card><Form form={form} layout="vertical" className="av-filter-grid" onFinish={(values: Record<string, string>) => {setFilters(Object.fromEntries(Object.entries(values).filter(([, value]) => value?.trim()).map(([key, value]) => [key, key === 'from' || key === 'to' ? new Date(value).toISOString() : value.trim()]))); setPage(1);}}>
      <Form.Item name="q" label="名称或编号"><Input allowClear placeholder={`搜索${name}名称或资源编号`}/></Form.Item>
      <Form.Item name="state" label="业务状态"><Select allowClear placeholder="全部状态" options={['scheduled', 'waiting', 'paired', 'active', 'live', 'ended', 'cancelled', 'expired'].map(value => ({value, label: stateNames[value]}))}/></Form.Item>
      <Form.Item name="owner" label="发起人编号"><Input allowClear placeholder="输入用户编号"/></Form.Item>
      <Form.Item name="from" label="创建时间起"><Input type="datetime-local"/></Form.Item>
      <Form.Item name="to" label="创建时间止"><Input type="datetime-local"/></Form.Item>
      <Space className="av-filter-actions"><Button type="primary" htmlType="submit">查询</Button><Button onClick={() => {form.resetFields(); setFilters({}); setPage(1);}}>重置</Button></Space>
    </Form></Card>
    {data.error && <RequestError error={data.error} retry={() => void data.refetch()}/>}
    <Card><Table className="av-desktop-table" rowKey="id" locale={emptyTable} loading={data.isLoading} dataSource={data.data?.items ?? []} columns={columns} pagination={false} scroll={{x: 800}}/>
      <div className="av-mobile-cards">{data.data?.items.map(row => <Card size="small" key={row.id} title={row.title || name} extra={<StateTag state={row.state}/>}><Typography.Paragraph className="av-id">{row.id}</Typography.Paragraph><Typography.Paragraph type="secondary">创建于 {formatTime(row.created)}</Typography.Paragraph><Button onClick={() => setSelected(row)}>查看详情</Button></Card>)}{!data.isLoading && !data.data?.items.length && <Empty description="当前条件下暂无记录"/>}</div>
      <Pagination className="av-pagination" current={page} pageSize={pageSize} total={data.data?.total ?? 0} pageSizeOptions={[20, 50, 100]} showSizeChanger showTotal={total => `共 ${total} 条`} onChange={(next, size) => {setPage(size === pageSize ? next : 1); setPageSize(size);}}/>
    </Card>
    {selected && <ResourceDrawer key={`${scope.identity}:${selected.id}`} resource={selected} onClose={() => setSelected(null)}/>}
  </div>;
}
